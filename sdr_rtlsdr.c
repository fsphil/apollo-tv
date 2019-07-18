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
#include <pthread.h>
#include <rtl-sdr.h>
#include "sdr.h"

#define BUF_LEN   16384
#define BUF_COUNT 4

typedef struct {
	
	rtlsdr_dev_t *dev;
	
	pthread_t thread;
	
	int16_t buf[BUF_COUNT][BUF_LEN];
	pthread_mutex_t mutex[BUF_COUNT];
	int buf_len;
	int in;
	int out;
	
} _state_t;

static void _rx_callback(uint8_t *buf, uint32_t len, void *ctx)
{
	_state_t *s = ctx;
	int i;
	
	if(len != BUF_LEN)
	{
		fprintf(stderr, "BUF_LEN != len (%d != %d)\n", BUF_LEN, len);
	}
	
	for(i = 0; i < BUF_LEN; i++)
	{
		s->buf[s->in][i] = (int16_t) buf[i] + INT8_MIN;
	}
	
	/* Try to get a lock on the next output buffer */
	i = (s->in + 1) % BUF_COUNT;
	
	if(pthread_mutex_trylock(&s->mutex[i]) != 0)
	{
		/* No luck, the reader must have it */
		fprintf(stderr, "O");
		return;
	}
	
	/* Got a lock on the next buffer, release the previous */
	pthread_mutex_unlock(&s->mutex[s->in]);
	s->in = i;
}

static void *_rx_thread(void *arg)
{
	_state_t *s = arg;
	
	rtlsdr_read_async(s->dev, _rx_callback, s, 0, BUF_LEN);
	
	return(0);
}

static int _sdr_read(sdr_t *d, int16_t *buffer, int samples)
{
	_state_t *s = d->_priv;
	int i;
	
	/* If the current output buffer is empty, try to move on */
	if(s->buf_len == 0)
	{
		/* Try to get a lock on the next output buffer */
		i = (s->out + 1) % BUF_COUNT;
		
		/* TODO: Add a timeout here */
		
		/* Get a lock on the next buffer, clear and release the previous */
		pthread_mutex_lock(&s->mutex[i]);
		pthread_mutex_unlock(&s->mutex[s->out]);
		
		s->out = i;
		s->buf_len = BUF_LEN;
	}
	
	samples *= 2;
	
	if(samples > s->buf_len)
	{
		samples = s->buf_len;
	}
	
	for(i = 0; i < samples; i++)
	{
		buffer[i] = s->buf[s->out][BUF_LEN - s->buf_len + i];
	}
	
	s->buf_len -= samples;
	
	return(samples / 2);
}

static void _sdr_close(sdr_t *d)
{
	_state_t *s = d->_priv;
	
	rtlsdr_cancel_async(s->dev);
	pthread_join(s->thread, NULL);
	
	rtlsdr_close(s->dev);
	
	free(s);
}

int sdr_open_rtlsdr(sdr_t *d, uint32_t index, uint32_t sample_rate, uint64_t frequency_hz, int gain, int error_ppm)
{
	int r;
	_state_t *s;
	
	s = calloc(sizeof(_state_t), 1);
	if(!s)
	{
		return(-1);
	}
	
	/* Open the device */
	r = rtlsdr_open(&s->dev, index);
	if(r < 0)
	{
		fprintf(stderr, "Failed to open rtlsdr device #%d\n", index);
		return(-1);
	}
	
	rtlsdr_set_sample_rate(s->dev, sample_rate);
	
	/* Enable AGC */
	rtlsdr_set_agc_mode(s->dev, 1);
	
	/* Disable antenna power */
	rtlsdr_set_bias_tee(s->dev, 0);
	
	/* Tune the radio */
	fprintf(stderr, "Settings frequency to %lu Hz...\n", frequency_hz);
	rtlsdr_set_center_freq(s->dev, frequency_hz);
	
	/* Automatic bandwidth */
	rtlsdr_set_tuner_bandwidth(s->dev, 0);
	
	/* Set error */
	rtlsdr_set_freq_correction(s->dev, error_ppm);
	
	/* Setup the links */
	d->_priv = s;
	d->read  = &_sdr_read;
	d->close = &_sdr_close;
	
	/* Prepare the in/out buffers */
	for(r = 0; r < BUF_COUNT; r++)
	{
		pthread_mutex_init(&s->mutex[r], NULL);
	}
	
	s->in  = 0;
	s->out = BUF_COUNT - 1;
	
	pthread_mutex_lock(&s->mutex[s->in]);
	pthread_mutex_lock(&s->mutex[s->out]);
	
	s->buf_len = 0;
	
	/* Begin the read thread */
	rtlsdr_reset_buffer(s->dev);
	pthread_create(&s->thread, NULL, _rx_thread, (void *) s);
	
	return(0);
}

