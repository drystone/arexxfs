/*
This file is part of arexxfs, a fuse interface to the arexx data loggers

Copyright © 2013 John Hedges <john@drystone.co.uk>

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

typedef struct _reading tlx_reading;

struct _reading {
    unsigned int id;
    int raw;
    time_t ctime;
    time_t mtime;
    tlx_reading * next;
};

extern time_t tlx_ctime, tlx_mtime;

void tlx_init();
tlx_reading * tlx_get_root();
tlx_reading * tlx_get_reading(const char * id);
