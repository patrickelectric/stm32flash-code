/*
  stm32flash - Open Source ST STM32 flash program for *nix
  Copyright 2010 Geoffrey McRae <geoff@spacevs.com>
  Copyright 2011 Steve Markgraf <steve@steve-m.de>
  Copyright 2012-2016 Tormod Volden <debian.tormod@gmail.com>
  Copyright 2013-2016 Antonio Borneo <borneo.antonio@gmail.com>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "stm32flasher.h"

#if defined(__WIN32__) || defined(__CYGWIN__)
#include <windows.h>
#endif

#define VERSION "0.5"

Stm32Flasher::Stm32Flasher()
{

}

const char *Stm32Flasher::action2str(enum actions act)
{
    switch (act) {
        case ACT_READ:
            return "memory read";
        case ACT_WRITE:
            return "memory write";
        case ACT_WRITE_UNPROTECT:
            return "write unprotect";
        case ACT_READ_PROTECT:
            return "read protect";
        case ACT_READ_UNPROTECT:
            return "read unprotect";
        case ACT_ERASE_ONLY:
            return "flash erase";
        case ACT_CRC:
            return "memory crc";
        default:
            return "";
    };
}

void Stm32Flasher::err_multi_action(enum actions newAct)
{
    fprintf(stderr,
        "ERROR: Invalid options !\n"
        "\tCan't execute \"%s\" and \"%s\" at the same time.\n",
        action2str(action), action2str(newAct));
}

int Stm32Flasher::is_addr_in_ram(uint32_t addr)
{
    return addr >= stm->dev->ram_start && addr < stm->dev->ram_end;
}

int Stm32Flasher::is_addr_in_flash(uint32_t addr)
{
    return addr >= stm->dev->fl_start && addr < stm->dev->fl_end;
}

int Stm32Flasher::is_addr_in_opt_bytes(uint32_t addr)
{
    /* option bytes upper range is inclusive in our device table */
    return addr >= stm->dev->opt_start && addr <= stm->dev->opt_end;
}

int Stm32Flasher::is_addr_in_sysmem(uint32_t addr)
{
    return addr >= stm->dev->mem_start && addr < stm->dev->mem_end;
}

/* returns the page that contains address "addr" */
int Stm32Flasher::flash_addr_to_page_floor(uint32_t addr)
{
    int page;
    uint32_t *psize;

    if (!is_addr_in_flash(addr))
        return 0;

    page = 0;
    addr -= stm->dev->fl_start;
    psize = stm->dev->fl_ps;

    while (addr >= psize[0]) {
        addr -= psize[0];
        page++;
        if (psize[1])
            psize++;
    }

    return page;
}

/* returns the first page whose start addr is >= "addr" */
int Stm32Flasher::flash_addr_to_page_ceil(uint32_t addr)
{
    int page;
    uint32_t *psize;

    if (!(addr >= stm->dev->fl_start && addr <= stm->dev->fl_end))
        return 0;

    page = 0;
    addr -= stm->dev->fl_start;
    psize = stm->dev->fl_ps;

    while (addr >= psize[0]) {
        addr -= psize[0];
        page++;
        if (psize[1])
            psize++;
    }

    return addr ? page + 1 : page;
}

/* returns the lower address of flash page "page" */
uint32_t Stm32Flasher::flash_page_to_addr(int page)
{
    int i;
    uint32_t addr, *psize;

    addr = stm->dev->fl_start;
    psize = stm->dev->fl_ps;

    for (i = 0; i < page; i++) {
        addr += psize[0];
        if (psize[1])
            psize++;
    }

    return addr;
}

void Stm32Flasher::sighandler(int s){
    fprintf(stderr, "\nCaught signal %d\n",s);
    if (p_st &&  parser ) parser->close(p_st);
    if (stm  ) stm32_close  (stm);
    if (port) port->close(port);
    exit(1);
}

int Stm32Flasher::flash() {
    msleep(250); // wait for stm32 bootloader

    int ret = 1;
    stm32_err_t s_err;
    parser_err_t perr;
    diag = stdout;
    action = ACT_WRITE;
    exec_flag = 1;
    execute = 0x0;
    port_opts.baudRate = SERIAL_BAUD_230400;
//  port_opts.baudRate = SERIAL_BAUD_460800;

    if (action == ACT_READ && use_stdinout) {
        diag = stderr;
    }

    fprintf(diag, "stm32flash " VERSION "\n\n");
    fprintf(diag, "http://stm32flash.sourceforge.net/\n\n");

// ctrl + c handling
//#if defined(__WIN32__) || defined(__CYGWIN__)
//    SetConsoleCtrlHandler( (PHANDLER_ROUTINE) CtrlHandler, TRUE );
//#else
//    struct sigaction sigIntHandler;

//    sigIntHandler.sa_handler = sighandler;
//    sigemptyset(&sigIntHandler.sa_mask);
//    sigIntHandler.sa_flags = 0;

//    sigaction(SIGINT, &sigIntHandler, NULL);
//#endif

    if (action == ACT_WRITE) {
        /* first try hex */
        if (!force_binary) {
            parser = &PARSER_HEX;
            p_st = parser->init();
            if (!p_st) {
                fprintf(stderr, "%s Parser failed to initialize\n", parser->name);
                return cleanup(ret);
            }
        }

        if (force_binary || (perr = parser->open(p_st, filename, 0)) != PARSER_ERR_OK) {
            if (force_binary || perr == PARSER_ERR_INVALID_FILE) {
                if (!force_binary) {
                    parser->close(p_st);
                    p_st = NULL;
                }

                /* now try binary */
                parser = &PARSER_BINARY;
                p_st = parser->init();
                if (!p_st) {
                    fprintf(stderr, "%s Parser failed to initialize\n", parser->name);
                    return cleanup(ret);
                }
                perr = parser->open(p_st, filename, 0);
            }

            /* if still have an error, fail */
            if (perr != PARSER_ERR_OK) {
                fprintf(stderr, "%s ERROR: %s\n", parser->name, parser_errstr(perr));
                if (perr == PARSER_ERR_SYSTEM) perror(filename);
                return cleanup(ret);
            }
        }

        fprintf(diag, "Using Parser : %s\n", parser->name);
    } else {
        parser = &PARSER_BINARY;
        p_st = parser->init();
        if (!p_st) {
            fprintf(stderr, "%s Parser failed to initialize\n", parser->name);
            return cleanup(ret);
        }
    }

    int serialTry = 3;
    while (serialTry && port_open(&port_opts, &port) != PORT_ERR_OK) {
        fprintf(stderr, "Failed to open port: %s\n", port_opts.device);
        sleep(1);
        serialTry--;
    }

    if(!serialTry) {
        return cleanup(ret);
    }

    fprintf(diag, "Interface %s: %s\n", port->name, port->get_cfg_str(port));
    if (init_flag && init_bl_entry(port, gpio_seq)){
        ret = 1;
        fprintf(stderr, "Failed to send boot enter sequence\n");
        return cleanup(ret);
    }

    port->flush(port);

    stm = stm32_init(port, init_flag);
    if (!stm)
        return cleanup(ret);

    fprintf(diag, "Version      : 0x%02x\n", stm->bl_version);
    if (port->flags & PORT_GVR_ETX) {
        fprintf(diag, "Option 1     : 0x%02x\n", stm->option1);
        fprintf(diag, "Option 2     : 0x%02x\n", stm->option2);
    }
    fprintf(diag, "Device ID    : 0x%04x (%s)\n", stm->pid, stm->dev->name);
    fprintf(diag, "- RAM        : Up to %dKiB  (%db reserved by bootloader)\n", (stm->dev->ram_end - 0x20000000) / 1024, stm->dev->ram_start - 0x20000000);
    fprintf(diag, "- Flash      : Up to %dKiB (size first sector: %dx%d)\n", (stm->dev->fl_end - stm->dev->fl_start ) / 1024, stm->dev->fl_pps, stm->dev->fl_ps[0]);
    fprintf(diag, "- Option RAM : %db\n", stm->dev->opt_end - stm->dev->opt_start + 1);
    fprintf(diag, "- System RAM : %dKiB\n", (stm->dev->mem_end - stm->dev->mem_start) / 1024);

    uint8_t  buffer[256];
    uint32_t addr, start, end;
    unsigned int len;
    int  failed = 0;
    int  first_page, num_pages;

    /*
     * Cleanup addresses:
     *
     * Starting from options
     * start_addr, readwrite_len, spage, npages
     * and using device memory size, compute
     * start, end, first_page, num_pages
     */
    if (start_addr || readwrite_len) {
        start = start_addr;

        if (is_addr_in_flash(start))
            end = stm->dev->fl_end;
        else {
            no_erase = 1;
            if (is_addr_in_ram(start))
                end = stm->dev->ram_end;
            else if (is_addr_in_opt_bytes(start))
                end = stm->dev->opt_end + 1;
            else if (is_addr_in_sysmem(start))
                end = stm->dev->mem_end;
            else {
                /* Unknown territory */
                if (readwrite_len)
                    end = start + readwrite_len;
                else
                    end = start + sizeof(uint32_t);
            }
        }

        if (readwrite_len && (end > start + readwrite_len))
            end = start + readwrite_len;

        first_page = flash_addr_to_page_floor(start);
        if (!first_page && end == stm->dev->fl_end)
            num_pages = STM32_MASS_ERASE;
        else
            num_pages = flash_addr_to_page_ceil(end) - first_page;
    } else if (!spage && !npages) {
        start = stm->dev->fl_start;
        end = stm->dev->fl_end;
        first_page = 0;
        num_pages = STM32_MASS_ERASE;
    } else {
        first_page = spage;
        start = flash_page_to_addr(first_page);
        if (start > stm->dev->fl_end) {
            fprintf(stderr, "Address range exceeds flash size.\n");
            return cleanup(ret);
        }

        if (npages) {
            num_pages = npages;
            end = flash_page_to_addr(first_page + num_pages);
            if (end > stm->dev->fl_end)
                end = stm->dev->fl_end;
        } else {
            end = stm->dev->fl_end;
            num_pages = flash_addr_to_page_ceil(end) - first_page;
        }

        if (!first_page && end == stm->dev->fl_end)
            num_pages = STM32_MASS_ERASE;
    }

    if (action == ACT_READ) {
        unsigned int max_len = port_opts.rx_frame_max;

        fprintf(diag, "Memory read\n");

        perr = parser->open(p_st, filename, 1);
        if (perr != PARSER_ERR_OK) {
            fprintf(stderr, "%s ERROR: %s\n", parser->name, parser_errstr(perr));
            if (perr == PARSER_ERR_SYSTEM)
                perror(filename);
            return cleanup(ret);
        }

        fflush(diag);
        addr = start;
        while(addr < end) {
            uint32_t left = end - addr;
            len  = max_len > left ? left : max_len;
            s_err = stm32_read_memory(stm, addr, buffer, len);
            if (s_err != STM32_ERR_OK) {
                fprintf(stderr, "Failed to read memory at address 0x%08x, target write-protected?\n", addr);
                return cleanup(ret);
            }
            if (parser->write(p_st, buffer, len) != PARSER_ERR_OK)
            {
                fprintf(stderr, "Failed to write data to file\n");
                return cleanup(ret);
            }
            addr += len;

            fprintf(diag,
                "\rRead address 0x%08x (%.2f%%) ",
                addr,
                (100.0f / (float)(end - start)) * (float)(addr - start)
            );
            fflush(diag);
        }
        fprintf(diag, "Done.\n");
        ret = 0;
        return cleanup(ret);
    } else if (action == ACT_READ_PROTECT) {
        fprintf(diag, "Read-Protecting flash\n");
        /* the device automatically performs a reset after the sending the ACK */
        reset_flag = 0;
        s_err = stm32_readprot_memory(stm);
        if (s_err != STM32_ERR_OK) {
            fprintf(stderr, "Failed to read-protect flash\n");
            return cleanup(ret);
        }
        fprintf(diag, "Done.\n");
        ret = 0;
    } else if (action == ACT_READ_UNPROTECT) {
        fprintf(diag, "Read-UnProtecting flash\n");
        /* the device automatically performs a reset after the sending the ACK */
        reset_flag = 0;
        s_err = stm32_runprot_memory(stm);
        if (s_err != STM32_ERR_OK) {
            fprintf(stderr, "Failed to read-unprotect flash\n");
            return cleanup(ret);
        }
        fprintf(diag, "Done.\n");
        ret = 0;
    } else if (action == ACT_ERASE_ONLY) {
        ret = 0;
        fprintf(diag, "Erasing flash\n");

        if (num_pages != STM32_MASS_ERASE &&
            (start != flash_page_to_addr(first_page)
             || end != flash_page_to_addr(first_page + num_pages))) {
            fprintf(stderr, "Specified start & length are invalid (must be page aligned)\n");
            ret = 1;
            return cleanup(ret);
        }

        s_err = stm32_erase_memory(stm, first_page, num_pages);
        if (s_err != STM32_ERR_OK) {
            fprintf(stderr, "Failed to erase memory\n");
            ret = 1;
            return cleanup(ret);
        }
        ret = 0;
    } else if (action == ACT_WRITE_UNPROTECT) {
        fprintf(diag, "Write-unprotecting flash\n");
        /* the device automatically performs a reset after the sending the ACK */
        reset_flag = 0;
        s_err = stm32_wunprot_memory(stm);
        if (s_err != STM32_ERR_OK) {
            fprintf(stderr, "Failed to write-unprotect flash\n");
            return cleanup(ret);
        }
        fprintf(diag, "Done.\n");
        ret = 0;
    } else if (action == ACT_WRITE) {
        fprintf(diag, "Write to memory\n");

        off_t  offset = 0;
        ssize_t r;
        unsigned int size;
        unsigned int max_wlen, max_rlen;

        max_wlen = port_opts.tx_frame_max - 2; /* skip len and crc */
        max_wlen &= ~3; /* 32 bit aligned */

        max_rlen = port_opts.rx_frame_max;
        max_rlen = max_rlen < max_wlen ? max_rlen : max_wlen;

        /* Assume data from stdin is whole device */
        if (use_stdinout)
            size = end - start;
        else
            size = parser->size(p_st);

        // TODO: It is possible to write to non-page boundaries, by reading out flash
        //       from partial pages and combining with the input data
        // if ((start % stm->dev->fl_ps[i]) != 0 || (end % stm->dev->fl_ps[i]) != 0) {
        // fprintf(stderr, "Specified start & length are invalid (must be page aligned)\n");
        // return cleanup(ret);
        // }

        // TODO: If writes are not page aligned, we should probably read out existing flash
        //       contents first, so it can be preserved and combined with new data
        if (!no_erase && num_pages) {
            fprintf(diag, "Erasing memory\n");
            s_err = stm32_erase_memory(stm, first_page, num_pages);
            if (s_err != STM32_ERR_OK) {
                fprintf(stderr, "Failed to erase memory\n");
                return cleanup(ret);
            }
        }

        fprintf(diag, "Writing memory\n");
        fflush(diag);
        addr = start;
        while(addr < end && offset < size) {
            uint32_t left = end - addr;
            len  = max_wlen > left ? left : max_wlen;
            len  = len > size - offset ? size - offset : len;

            if (parser->read(p_st, buffer, &len) != PARSER_ERR_OK)
                return cleanup(ret);

            if (len == 0) {
                if (use_stdinout) {
                    break;
                } else {
                    fprintf(stderr, "Failed to read input file\n");
                    return cleanup(ret);
                }
            }

            again:
            s_err = stm32_write_memory(stm, addr, buffer, len);
            if (s_err != STM32_ERR_OK) {
                fprintf(stderr, "Failed to write memory at address 0x%08x\n", addr);
                return cleanup(ret);
            }

            if (_verify) {
                uint8_t compare[len];
                unsigned int offset, rlen;

                offset = 0;
                while (offset < len) {
                    rlen = len - offset;
                    rlen = rlen < max_rlen ? rlen : max_rlen;
                    s_err = stm32_read_memory(stm, addr + offset, compare + offset, rlen);
                    if (s_err != STM32_ERR_OK) {
                        fprintf(stderr, "Failed to read memory at address 0x%08x\n", addr + offset);
                        return cleanup(ret);
                    }
                    offset += rlen;
                }

                for(r = 0; r < len; ++r)
                    if (buffer[r] != compare[r]) {
                        if (failed == retry) {
                            fprintf(stderr, "Failed to verify at address 0x%08x, expected 0x%02x and found 0x%02x\n",
                                (uint32_t)(addr + r),
                                buffer [r],
                                compare[r]
                            );
                            return cleanup(ret);
                        }
                        ++failed;
                        goto again;
                    }

                failed = 0;
            }

            addr += len;
            offset += len;

//            fprintf(diag,
//                "\rWrote %saddress 0x%08x (%.2f%%) ",
//                _verify ? "and verified " : "",
//                addr,
//                (100.0f / size) * offset
//            );
//            fflush(diag);

            emit flashProgress((100.0f / size) * offset);
        }

        fprintf(diag, "Done.\n");
        ret = 0;
        return cleanup(ret);
    } else if (action == ACT_CRC) {
        uint32_t crc_val = 0;

        fprintf(diag, "CRC computation\n");

        s_err = stm32_crc_wrapper(stm, start, end - start, &crc_val);
        if (s_err != STM32_ERR_OK) {
            fprintf(stderr, "Failed to read CRC\n");
            return cleanup(ret);
        }
        fprintf(diag, "CRC(0x%08x-0x%08x) = 0x%08x\n", start, end,
            crc_val);
        ret = 0;
        return cleanup(ret);
    } else
        ret = 0;

    return cleanup(ret);
}

int Stm32Flasher::cleanup(int ret)
{
    if (stm && exec_flag && ret == 0) {
        if (execute == 0)
            execute = stm->dev->fl_start;

        fprintf(diag, "\nStarting execution at address 0x%08x... ", execute);
        fflush(diag);
        if (stm32_go(stm, execute) == STM32_ERR_OK) {
            reset_flag = 0;
            fprintf(diag, "done.\n");
        } else
            fprintf(diag, "failed.\n");
    }

    if (stm && reset_flag) {
        fprintf(diag, "\nResetting device... \n");
        fflush(diag);
        if (init_bl_exit(stm, port, gpio_seq)) {
            ret = 1;
            fprintf(diag, "Reset failed.\n");
        } else
            fprintf(diag, "Reset done.\n");
    } else if (port) {
        /* Always run exit sequence if present */
        if (gpio_seq && strchr(gpio_seq, ':'))
            ret = gpio_bl_exit(port, gpio_seq) || ret;
    }

    if (p_st  ) parser->close(p_st);
    if (stm   ) stm32_close  (stm);
    if (port)
        port->close(port);

    fprintf(diag, "\n");
    fflush(diag);

    emit flashComplete(ret);
    return ret;
}
