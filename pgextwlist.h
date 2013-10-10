/* PostgreSQL Extension WhiteList -- Dimitri Fontaine
 *
 * Author: Dimitri Fontaine <dimitri@2ndQuadrant.fr>
 * Licence: PostgreSQL
 * Copyright Dimitri Fontaine, 2011-2013
 *
 * For a description of the features see the README.md file from the same
 * distribution.
 */

#ifdef PG_VERSION_NUM
#define PG_MAJOR_VERSION (PG_VERSION_NUM / 100)
#else
#error "Unknown PostgreSQL version"
#endif

#if PG_MAJOR_VERSION != 901    \
	&& PG_MAJOR_VERSION != 902 \
	&& PG_MAJOR_VERSION != 903 \
	&& PG_MAJOR_VERSION != 904
#error "Unsupported postgresql version"
#endif

