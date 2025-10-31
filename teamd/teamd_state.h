/*
 *   teamd_state.h - Teamd state frontend
 *   Copyright (C) 2013-2015 Jiri Pirko <jiri@resnulli.us>
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _TEAMD_STATE_H_
#define _TEAMD_STATE_H_

#include "teamd.h"

enum teamd_state_val_type {
	TEAMD_STATE_ITEM_TYPE_NODE = 0,
	TEAMD_STATE_ITEM_TYPE_INT,
	TEAMD_STATE_ITEM_TYPE_STRING,
	TEAMD_STATE_ITEM_TYPE_BOOL,
};

struct team_state_gsc {
	union {
		int int_val;
		struct {
			const char *ptr;
			bool free;
		} str_val;
		bool bool_val;
	} data;
	struct {
		struct teamd_port *tdport;
	} info;
};

struct teamd_state_val {
	const char *subpath;
	enum teamd_state_val_type type;
	int (*getter)(struct teamd_context *ctx,
		      struct team_state_gsc *gsc, void *priv);
	int (*setter)(struct teamd_context *ctx,
		      struct team_state_gsc *gsc, void *priv);
	const struct teamd_state_val *vals;
	unsigned int vals_count;
	bool per_port;
};

int teamd_state_val_register_ex(struct teamd_context *ctx,
				const struct teamd_state_val *val,
				void *priv, struct teamd_port *tdport,
				const char *fmt, ...);
int teamd_state_val_register(struct teamd_context *ctx,
			     const struct teamd_state_val *val,
			     void *priv);
void teamd_state_val_unregister(struct teamd_context *ctx,
				const struct teamd_state_val *val,
				void *priv);
int teamd_state_init(struct teamd_context *ctx);
void teamd_state_fini(struct teamd_context *ctx);
int teamd_state_dump(struct teamd_context *ctx, char **p_state_dump);
int teamd_state_item_value_get(struct teamd_context *ctx, const char *item_path,
			       char **p_value);
int teamd_state_item_value_set(struct teamd_context *ctx, const char *item_path,
			       const char *value);

int teamd_state_basics_init(struct teamd_context *ctx);
void teamd_state_basics_fini(struct teamd_context *ctx);

#endif /* _TEAMD_STATE_H_ */
