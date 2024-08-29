/**
 * Copyright © 2024 Fraawlen <fraawlen@posteo.net>
 *
 * This file is part of the program.
 *
 * This library is free software; you can redistribute it and/or modify it either under the terms of the GNU
 * Affero General Public License as published by the Free Software Foundation; either version 3.0  of the
 * License or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.
 * See the LGPL for the specific language governing rights and limitations.
 *
 * You should have received a copy of the GNU Lesser General Public License along with this program. If not,
 * see <http://www.gnu.org/licenses/>.
 */

/************************************************************************************************************/
/************************************************************************************************************/
/************************************************************************************************************/

#include <dg/core/core.h>
#include <dg/base/base.h>
#include <dg/base/origin.h>
#include <dg/base/string.h>
#include <float.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/sysinfo.h>
#include <unistd.h>

/************************************************************************************************************/
/************************************************************************************************************/
/************************************************************************************************************/

#define PROGRAM "sysgauges"
#define VERSION "v.1.0.0"
#define GB      * data.mem_unit / 1073741824.0

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

struct row
{
	const char *name;
	const char *unit;
	const int precision;
	const bool custom_max;
	dg_core_cell_t *label;
	dg_core_cell_t *gauge;
	dg_core_cell_t *max;
};

/************************************************************************************************************/
/************************************************************************************************************/
/************************************************************************************************************/

static void  _help        (void);
static void  _options     (int, char**);
static void  _resize      (void);
static void  _row_destroy (struct row *);
static void  _row_setup   (struct row *, double);
static void  _row_update  (struct row *, double, double);
static void *_thread      (void *);
static void  _update_all  (uint32_t);

/************************************************************************************************************/
/************************************************************************************************************/
/************************************************************************************************************/

/* - User parameters - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static bool         _show_max = false;
static bool         _verbose  = false;
static double       _alert    = 0.95;
static unsigned int _delay    = 1;
static int16_t      _width    = 0;
static int16_t      _x        = 20;
static int16_t      _y        = 20;

/* GUI components  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static dg_core_window_t *_w = NULL;
static dg_core_grid_t   *_g = NULL;

static struct row _cpu = { "CPU", "%",  2, false, NULL, NULL, NULL };
static struct row _mem = { "MEM", "GB", 1, true,  NULL, NULL, NULL };
static struct row _swp = { "SWP", "GB", 1, true,  NULL, NULL, NULL };

static int16_t _pos = 0;

/* Misc  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static pthread_t _t;

/************************************************************************************************************/
/* MAIN *****************************************************************************************************/
/************************************************************************************************************/

int
main(int argc, char **argv)
{
	struct sysinfo data;

	/* Setup */

	_options(argc, argv);

	dg_core_init(argc, argv, NULL, NULL, NULL);
	dg_base_init();
	sysinfo(&data);
	pthread_create(&_t, NULL, _thread, NULL);

	_w = dg_core_window_create(DG_CORE_WINDOW_FIXED);
	_g = dg_core_grid_create(3, data.totalswap > 0 ? 3 : 2);

	/* Grid configuration */

	dg_core_grid_set_column_growth(_g, 1, 1.0);
	dg_core_grid_set_column_width(_g, 0, 3);
	dg_core_grid_set_column_width(_g, 1, 32);
	dg_core_grid_set_column_width(_g, 2, 6);

	/* Rows configuration */

	_row_setup(&_cpu, 100.0);
	_row_setup(&_mem, data.totalram  GB);
	_row_setup(&_swp, data.totalswap GB);

	/* Window configuration */

	_resize();

	dg_core_window_push_grid(_w, _g);
	dg_core_window_rename(_w, "sysmeter", NULL);
	dg_core_window_activate(_w);
	dg_core_window_set_fixed_position(_w, _x, _y);

	/* Run */

	dg_core_resource_set_callback(_resize);
	dg_core_loop_set_callback_signal(_update_all);
	dg_core_loop_run();

	/* Cleanup & end */

	dg_core_window_destroy(_w);
	dg_core_grid_destroy(_g);

	_row_destroy(&_cpu);
	_row_destroy(&_mem);
	_row_destroy(&_swp);

	dg_base_reset();
	dg_core_reset();

	pthread_join(_t, NULL);

	return 0;
}

/************************************************************************************************************/
/* STATIC ***************************************************************************************************/
/************************************************************************************************************/

static void
_help(void)
{
	printf(
		PROGRAM " " VERSION "\n"
		"usage: " PROGRAM " [option] <value>\n"
		"\t-a <0.0..1.0> : alert threshold\n"
		"\t-h            : print this help\n"
		"\t-i <uint>     : update interval in seconds\n"
		"\t-m            : show max MEM and SWP values\n"
		"\t-v            : print extra information (window width and height)\n"
		"\t-w <uint16_t> : custom width\n"
		"\t-x <int16_t>  : custom x coordinate\n"
		"\t-y <int16_t>  : custom y coordinate\n");
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static void
_options(int argc, char **argv)
{
	int opt;

	while ((opt = getopt(argc, argv, "a:hi:mvw:x:y:")) != -1)
	{
		switch (opt)
		{
			case 'a':
				_alert = strtod(optarg, NULL);
				break;

			case 'h':
				_help();
				exit(0);

			case 'i':
				_delay = strtoul(optarg, NULL, 0);
				break;

			case 'm':
				_show_max = true;
				break;

			case 'v':
				_verbose = true;
				break;

			case 'w':
				_width = strtoul(optarg, NULL, 0);
				break;

			case 'x':
				_x = strtol(optarg, NULL, 0);
				break;

			case 'y':
				_y = strtol(optarg, NULL, 0);
				break;

			case '?':
			default :
				fprintf(stderr, "try \'" PROGRAM " -h\' for more information\n");
				exit(0);
		}
	}
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static void
_resize(void)
{
	int16_t width  = dg_core_grid_get_min_pixel_width(_g);
	int16_t height = dg_core_grid_get_min_pixel_height(_g);

	dg_core_window_set_fixed_size(_w, _width > width ? _width : width, height);

	if (_verbose)
	{
		printf("window size updated\n");
		printf("width  = %i\n", width);
		printf("height = %i\n", height);
	}
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static void
_row_destroy(struct row *r)
{
	dg_core_cell_destroy(r->label);
	dg_core_cell_destroy(r->gauge);
	dg_core_cell_destroy(r->max);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static void
_row_setup(struct row *r, double max)
{
	dg_base_string_t tmp;

	r->label = dg_base_indicator_create();
	r->gauge = dg_base_gauge_create();
	r->max   = dg_base_label_create();

	if (max <= DBL_EPSILON)
	{
		return;
	}

	tmp = dg_base_string_convert_double(max, r->precision);

	dg_base_string_append(&tmp, r->unit);
	dg_base_gauge_set_label_style(r->gauge, r->precision, r->unit);
	dg_base_gauge_set_limits(r->gauge, 0.0, max);
	dg_base_indicator_set_label(r->label, r->name);
	dg_base_label_set_label(r->max, tmp.chars);
	dg_base_label_set_origin(r->max, DG_BASE_ORIGIN_RIGHT);
	dg_base_string_clear(&tmp);

	dg_core_grid_assign_cell(_g, r->label, 0, _pos, 1, 1);
	if (_show_max && r->custom_max)
	{
		dg_core_grid_assign_cell(_g, r->gauge, 1, _pos, 1, 1);
		dg_core_grid_assign_cell(_g, r->max,   2, _pos, 1, 1);
	}
	else
	{
		dg_core_grid_assign_cell(_g, r->gauge, 1, _pos, 2, 1);
	}

	_pos++;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static void
_row_update(struct row *r, double val, double high)
{
	dg_base_gauge_set_value(r->gauge, val);

	if (val >= high)
	{
		dg_base_indicator_set_on(r->label);
	}
	else
	{
		dg_base_indicator_set_off(r->label);
	}
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static void *
_thread(void *params)
{
	(void)params;

	while (dg_core_is_init())
	{
		dg_core_loop_send_signal(0);
		sleep(_delay);
	}

	pthread_exit(NULL);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

static void
_update_all(uint32_t serial)
{
	struct sysinfo data;

	(void)serial;

	sysinfo(&data);

	_row_update(&_cpu, data.loads[0] * 100.0 / get_nprocs() / (1 << SI_LOAD_SHIFT), 100.0 * _alert);
	_row_update(&_mem, (data.totalram  - data.freeram)  GB, data.totalram  GB * _alert);
	_row_update(&_swp, (data.totalswap - data.freeswap) GB, data.totalswap GB * _alert);
}
