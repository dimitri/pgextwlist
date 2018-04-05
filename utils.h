/* PostgreSQL Extension WhiteList -- Dimitri Fontaine
 *
 * Author: Dimitri Fontaine <dimitri@2ndQuadrant.fr>
 * Licence: PostgreSQL
 * Copyright Dimitri Fontaine, 2011-2013
 *
 * For a description of the features see the README.md file from the same
 * distribution.
 */

#ifndef __UTILS_H__
#define __UTILS_H__

#include "utils/builtins.h"
#include "nodes/pg_list.h"

#define MAXPGPATH 1024

extern char *extwlist_extensions;
extern char *extwlist_custom_path;

char *get_specific_custom_script_filename(const char *name,
										  const char *when,
										  const char *from_version,
										  const char *version);

char *get_generic_custom_script_filename(const char *name,
										 const char *action,
										 const char *when);

char *get_extension_current_version(const char *extname);

void fill_in_extension_properties(const char *extname,
								  List *options,
								  char **schema,
								  char **old_version,
								  char **new_version);

void execute_custom_script(const char *schemaName, const char *filename);

char *get_current_user_name(void);
char *get_current_database_owner_name(void);

#endif
