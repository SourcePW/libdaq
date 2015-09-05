/*
** Copyright (C) 2015 Cisco and/or its affiliates. All rights reserved.
** Author: Michael R. Altizer <mialtize@cisco.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "daq.h"
#include "daq_api.h"
#include "daq_api_internal.h"
#include "daq_common.h"

typedef struct _daq_dict_entry
{
    char *key;
    char *value;
    struct _daq_dict_entry *next;
} DAQ_DictEntry_t;

typedef struct _daq_dict
{
    DAQ_DictEntry_t *entries;
    DAQ_DictEntry_t *iterator;
} DAQ_Dict_t;

typedef struct _daq_config
{
    const DAQ_Module_t *module;     /* Module that will be instantiated with this configuration */
    char *input;            /* Name of the interface(s) or file to be opened */
    int snaplen;            /* Maximum packet capture length */
    unsigned timeout;       /* Read timeout for acquire loop in milliseconds (0 = unlimited) */
    DAQ_Mode mode;          /* Module mode (DAQ_MODE_*) */
    uint32_t flags;         /* Other configuration flags (DAQ_CFG_*) */
    DAQ_Dict_t variables;   /* Dictionary of arbitrary key[:value] string pairs */
} DAQ_Config_t;


/*
 * DAQ Dictionary Functions
 */

static int daq_dict_insert_entry(DAQ_Dict_t *dict, const char *key, const char *value)
{
    DAQ_DictEntry_t *entry;

    entry = calloc(1, sizeof(DAQ_DictEntry_t));
    if (!entry)
        return DAQ_ERROR_NOMEM;
    entry->key = strdup(key);
    if (!entry->key)
    {
        free(entry);
        return DAQ_ERROR_NOMEM;
    }
    if (value)
    {
        entry->value = strdup(value);
        if (!entry->value)
        {
            free(entry->key);
            free(entry);
            return DAQ_ERROR_NOMEM;
        }
    }
    entry->next = dict->entries;
    dict->entries = entry;

    return DAQ_SUCCESS;
}

static DAQ_DictEntry_t *daq_dict_find_entry(DAQ_Dict_t *dict, const char *key)
{
    DAQ_DictEntry_t *entry;

    for (entry = dict->entries; entry; entry = entry->next)
    {
        if (!strcmp(entry->key, key))
            return entry;
    }

    return NULL;
}

static void daq_dict_delete_entry(DAQ_Dict_t *dict, const char *key)
{
    DAQ_DictEntry_t *entry, *prev = NULL;

    for (entry = dict->entries; entry; entry = entry->next)
    {
        if (!strcmp(entry->key, key))
        {
            if (prev)
                prev->next = entry->next;
            else
                dict->entries = entry->next;
            free(entry->key);
            free(entry->value);
            free(entry);
            dict->iterator = NULL;
            return;
        }
        prev = entry;
    }
}

static void daq_dict_clear(DAQ_Dict_t *dict)
{
    DAQ_DictEntry_t *entry;

    while ((entry = dict->entries))
    {
        dict->entries = entry->next;
        free(entry->key);
        free(entry->value);
        free(entry);
    }
    dict->iterator = NULL;
}

static DAQ_DictEntry_t *daq_dict_first_entry(DAQ_Dict_t *dict)
{
    dict->iterator = dict->entries;

    return dict->iterator;
}

static DAQ_DictEntry_t *daq_dict_next_entry(DAQ_Dict_t *dict)
{
    if (dict->iterator)
        dict->iterator = dict->iterator->next;

    return dict->iterator;
}


/*
 * DAQ Configuration Functions
 */

DAQ_LINKAGE int daq_config_new(DAQ_Config_t **cfgptr, const DAQ_Module_t *module)
{
    DAQ_Config_t *cfg;

    if (!cfgptr || !module)
        return DAQ_ERROR_INVAL;

    cfg = calloc(1, sizeof(DAQ_Config_t));
    if (!cfg)
        return DAQ_ERROR_NOMEM;

    cfg->module = module;
    *cfgptr = cfg;

    return DAQ_SUCCESS;
}

DAQ_LINKAGE const DAQ_Module_t *daq_config_get_module(DAQ_Config_h cfg)
{
    if (!cfg)
        return NULL;

    return cfg->module;
}

DAQ_LINKAGE int daq_config_set_input(DAQ_Config_t *cfg, const char *input)
{
    if (!cfg)
        return DAQ_ERROR_INVAL;

    if (cfg->input)
    {
        free(cfg->input);
        cfg->input = NULL;
    }

    if (input)
    {
        cfg->input = strdup(input);
        if (!cfg->input)
            return DAQ_ERROR_NOMEM;
    }

    return DAQ_SUCCESS;
}

DAQ_LINKAGE const char *daq_config_get_input(DAQ_Config_t *cfg)
{
    if (cfg)
        return cfg->input;

    return NULL;
}

DAQ_LINKAGE int daq_config_set_snaplen(DAQ_Config_t *cfg, int snaplen)
{
    if (!cfg)
        return DAQ_ERROR_INVAL;

    cfg->snaplen = snaplen;

    return DAQ_SUCCESS;
}

DAQ_LINKAGE int daq_config_get_snaplen(DAQ_Config_t *cfg)
{
    if (cfg)
        return cfg->snaplen;

    return 0;
}

DAQ_LINKAGE int daq_config_set_timeout(DAQ_Config_t *cfg, unsigned timeout)
{
    if (!cfg)
        return DAQ_ERROR_INVAL;

    cfg->timeout = timeout;

    return DAQ_SUCCESS;
}

DAQ_LINKAGE unsigned daq_config_get_timeout(DAQ_Config_t *cfg)
{
    if (cfg)
        return cfg->timeout;

    return 0;
}

DAQ_LINKAGE int daq_config_set_mode(DAQ_Config_t *cfg, DAQ_Mode mode)
{
    if (!cfg)
        return DAQ_ERROR_INVAL;

    cfg->mode = mode;

    return DAQ_SUCCESS;
}

DAQ_LINKAGE DAQ_Mode daq_config_get_mode(DAQ_Config_t *cfg)
{
    if (cfg)
        return cfg->mode;

    return DAQ_MODE_NONE;
}

DAQ_LINKAGE int daq_config_set_flag(DAQ_Config_t *cfg, uint32_t flag)
{
    if (!cfg)
        return DAQ_ERROR_INVAL;

    cfg->flags |= flag;

    return DAQ_SUCCESS;
}

DAQ_LINKAGE uint32_t daq_config_get_flags(DAQ_Config_t *cfg)
{
    if (cfg)
        return cfg->flags;

    return 0;
}

DAQ_LINKAGE int daq_config_set_variable(DAQ_Config_t *cfg, const char *key, const char *value)
{
    DAQ_DictEntry_t *entry;
    char *new_value;

    if (!cfg || !key)
        return DAQ_ERROR_INVAL;

    entry = daq_dict_find_entry(&cfg->variables, key);
    if (!entry)
        return daq_dict_insert_entry(&cfg->variables, key, value);

    if (value)
    {
        new_value = strdup(value);
        if (!new_value)
            return DAQ_ERROR_NOMEM;
        if (entry->value)
            free(entry->value);
        entry->value = new_value;
    }
    else if (entry->value)
    {
        free(entry->value);
        entry->value = NULL;
    }

    DEBUG("Set config dictionary entry '%s' => '%s'.\n", key, value);

    return DAQ_SUCCESS;
}

DAQ_LINKAGE const char *daq_config_get_variable(DAQ_Config_t *cfg, const char *key)
{
    DAQ_DictEntry_t *entry;

    if (!cfg || !key)
        return NULL;

    entry = daq_dict_find_entry(&cfg->variables, key);
    if (!entry)
        return NULL;

    return entry->value;
}

DAQ_LINKAGE void daq_config_delete_variable(DAQ_Config_t *cfg, const char *key)
{
    if (!cfg || !key)
        return;

    daq_dict_delete_entry(&cfg->variables, key);
}

DAQ_LINKAGE int daq_config_first_variable(DAQ_Config_t *cfg, const char **key, const char **value)
{
    DAQ_DictEntry_t *entry;

    if (!cfg || !key || !value)
        return DAQ_ERROR_INVAL;

    entry = daq_dict_first_entry(&cfg->variables);
    if (entry)
    {
        *key = entry->key;
        *value = entry->value;
    }
    else
    {
        *key = NULL;
        *value = NULL;
    }

    return DAQ_SUCCESS;
}

DAQ_LINKAGE int daq_config_next_variable(DAQ_Config_t *cfg, const char **key, const char **value)
{
    DAQ_DictEntry_t *entry;

    if (!cfg || !key || !value)
        return DAQ_ERROR_INVAL;

    entry = daq_dict_next_entry(&cfg->variables);
    if (entry)
    {
        *key = entry->key;
        *value = entry->value;
    }
    else
    {
        *key = NULL;
        *value = NULL;
    }
    return DAQ_SUCCESS;
}

DAQ_LINKAGE void daq_config_clear_variables(DAQ_Config_t *cfg)
{
    if (!cfg)
        return;

    daq_dict_clear(&cfg->variables);
}

DAQ_LINKAGE void daq_config_destroy(DAQ_Config_t *cfg)
{
    if (!cfg)
        return;

    free(cfg->input);
    daq_config_clear_variables(cfg);
    free(cfg);
}
