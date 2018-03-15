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

#pragma once

#include <QObject>
#include <QtConcurrent>

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "init.h"
#include "utils.h"
#include "serial.h"
#include "stm32.h"
#include "parsers/parser.h"
#include "port.h"

#include "parsers/binary.h"
#include "parsers/hex.h"
}

class Stm32Flasher : public QThread
{
    Q_OBJECT
public:
    Stm32Flasher();

    void flash(const QString& portName,const QString& fileName, bool verify = true) {
        _portName = portName;
        _fileName = fileName;
        _verify = verify;
        start();
    };

signals:
    void flashProgress(float progress);
    void flashComplete(bool success);

protected:
    void run() override {
        char ftmp[_fileName.length()];
        strcpy(ftmp, _fileName.toLatin1().data());
        filename = ftmp;

        char dtmp[_portName.length()];
        strcpy(dtmp, _portName.toLatin1().data());
        port_opts.device = dtmp;

        flash();
    }

private:
    QString _portName;
    QString _fileName;

    /* "device globals" */
    stm32_t               *stm    = NULL;
    void                  *p_st   = NULL;
    parser_t              *parser = NULL;
    struct port_interface *port   = NULL;

    /* settings */
    struct port_options port_opts = {
        .device          = NULL,
        .baudRate        = SERIAL_BAUD_57600,
        .serial_mode     = "8e1",
        .bus_addr        = 0,
        .rx_frame_max    = STM32_MAX_RX_FRAME,
        .tx_frame_max    = STM32_MAX_TX_FRAME,
    };

    enum actions {
        ACT_NONE,
        ACT_READ,
        ACT_WRITE,
        ACT_WRITE_UNPROTECT,
        ACT_READ_PROTECT,
        ACT_READ_UNPROTECT,
        ACT_ERASE_ONLY,
        ACT_CRC
    };

    enum actions  action        = ACT_NONE;
    int           npages        = 0;
    int           spage         = 0;
    int           no_erase      = 0;
    char          _verify       = 0;
    int           retry         = 10;
    char          exec_flag     = 0;
    uint32_t      execute       = 0;
    char          init_flag     = 1;
    int           use_stdinout  = 0;
    char          force_binary  = 0;
    FILE          *diag;
    char          reset_flag    = 0;
    const char    *filename     = NULL;
    char          *gpio_seq     = NULL;
    uint32_t      start_addr    = 0;
    uint32_t      readwrite_len = 0;

    const char *action2str(enum actions act);
    void err_multi_action(enum actions newAct);
    int is_addr_in_ram(uint32_t addr);
    int is_addr_in_flash(uint32_t addr);
    int is_addr_in_opt_bytes(uint32_t addr);
    int is_addr_in_sysmem(uint32_t addr);
    int flash_addr_to_page_floor(uint32_t addr);
    int flash_addr_to_page_ceil(uint32_t addr);
    uint32_t flash_page_to_addr(int page);
#if defined(__WIN32__) || defined(__CYGWIN__)
    BOOL CtrlHandler( DWORD fdwCtrlType );
#else
    void sighandler(int s);
#endif

    int flash();
    int cleanup(int ret);
};

