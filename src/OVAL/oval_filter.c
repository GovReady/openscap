/**
 * @file oval_filter.c
 * @brief OVAL filter data type implementation
 * @author "Tomas Heinrich" <theinric@redhat.com>
 *
 * @addtogroup OVALDEF
 * @{
 */
/*
 * Copyright 2010 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors:
 *      "Tomas Heinrich" <theinric@redhat.com>
 */
#include <config.h>
#include <string.h>
#include <stdlib.h>
#include "common/debug_priv.h"
#include "oval_definitions_impl.h"

struct oval_filter {
	struct oval_definition_model *model;
	struct oval_state *state;
	oval_filter_action_t action;
};

struct oval_filter *oval_filter_new(struct oval_definition_model *model)
{
	struct oval_filter *filter;

	filter = (struct oval_filter *) oscap_alloc(sizeof (struct oval_filter));
	if (filter == NULL)
		return NULL;

	filter->model = model;
	filter->state = NULL;
	filter->action = OVAL_FILTER_ACTION_UNKNOWN;
	return filter;
}

void oval_filter_free(struct oval_filter *filter)
{
	__attribute__nonnull__(filter);

	filter->model = NULL;
	filter->state = NULL;
	oscap_free(filter);
}

struct oval_filter *oval_filter_clone(struct oval_definition_model *new_model,
				      struct oval_filter *old_filter)
{
	// todo
	return NULL;
}

bool oval_filter_iterator_has_more(struct oval_filter_iterator *oc_filter)
{
	return oval_collection_iterator_has_more((struct oval_iterator *) oc_filter);
}

struct oval_filter *oval_filter_iterator_next(struct oval_filter_iterator *oc_filter)
{
	return (struct oval_filter *)
		oval_collection_iterator_next((struct oval_iterator *) oc_filter);
}

void oval_filter_iterator_free(struct oval_filter_iterator *oc_filter)
{
	oval_collection_iterator_free((struct oval_iterator *) oc_filter);
}

struct oval_state *oval_filter_get_state(struct oval_filter *filter)
{
	__attribute__nonnull__(filter);

	return filter->state;
}

oval_filter_action_t oval_filter_get_filter_action(struct oval_filter *filter)
{
	__attribute__nonnull__(filter);

	return filter->action;
}

bool oval_filter_is_locked(struct oval_filter *filter)
{
	__attribute__nonnull__(filter);

	return oval_definition_model_is_locked(filter->model);
}

bool oval_filter_is_valid(struct oval_filter *filter)
{
	// todo
	return true;
}

void oval_filter_set_state(struct oval_filter *filter, struct oval_state *state)
{
	if (filter && !oval_filter_is_locked(filter)) {
		filter->state = state;
	} else {
		oscap_dlprintf(DBG_W, "Attempt to update locked content.\n");
	}
}

void oval_filter_set_filter_action(struct oval_filter *filter, oval_filter_action_t action)
{
	if (filter && !oval_filter_is_locked(filter)) {
		filter->action = action;
	} else {
		oscap_dlprintf(DBG_W, "Attempt to update locked content.\n");
	}
}

int oval_filter_parse_tag(xmlTextReaderPtr reader, struct oval_parser_context *context,
			  oval_filter_consumer consumer, void *user)
{
	// todo
	return 1;
}

xmlNode *oval_filter_to_dom(struct oval_filter *filter, xmlDoc *doc, xmlNode *parent)
{
	// todo
	return NULL;
}