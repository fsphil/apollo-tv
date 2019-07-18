/* apollo-tv - Apollo Unified S-Band TV viewer                           */
/*=======================================================================*/
/* Copyright 2019 Philip Heron <phil@sanslogic.co.uk>                    */
/*                                                                       */
/* This program is free software: you can redistribute it and/or modify  */
/* it under the terms of the GNU General Public License as published by  */
/* the Free Software Foundation, either version 3 of the License, or     */
/* (at your option) any later version.                                   */
/*                                                                       */
/* This program is distributed in the hope that it will be useful,       */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/* GNU General Public License for more details.                          */
/*                                                                       */
/* You should have received a copy of the GNU General Public License     */
/* along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "sdr.h"

typedef struct {
	FILE *f;
} _state_t;

static int _sdr_read(sdr_t *d, int16_t *buffer, int samples)
{
	_state_t *s = d->_priv;
	uint8_t buf[2048];
	int i;
	
	samples = fread(buf, sizeof(int8_t) * 2, samples, s->f);
	
	for(i = 0; i < samples * 2; i++)
	{
		buffer[i] = buf[i] + INT8_MIN;
	}
	
	return(samples);
}

static void _sdr_close(sdr_t *d)
{
	_state_t *s = d->_priv;
	fclose(s->f);
	free(s);
}

int sdr_open_file(sdr_t *d, const char *name)
{
	_state_t *s;
	
	s = calloc(sizeof(_state_t), 1);
	if(!s)
	{
		return(-1);
	}
	
	/* Open the file */
	s->f = fopen(name, "rb");
	if(!s->f)
	{
		perror("fopen");
		return(-1);
	}
	
	/* Setup the links */
	d->_priv = s;
	d->read  = &_sdr_read;
	d->close = &_sdr_close;
	
	return(0);
}

