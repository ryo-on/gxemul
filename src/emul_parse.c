/*
 *  Copyright (C) 2005  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE   
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 *
 *
 *  $Id: emul_parse.c,v 1.8 2005-01-28 00:35:23 debug Exp $
 *
 *  Set up an emulation by parsing a config file.
 *
 *
 *  TODO: This could be extended to support XML config files as well, but
 *        XML is ugly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emul.h"
#include "machine.h"
#include "misc.h"
#include "net.h"


#define is_word_char(ch)	( \
	(ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || \
	ch == '_' || ch == '$' || (ch >= '0' && ch <= '9') )


#define	EXPECT_WORD			1
#define	EXPECT_LEFT_PARENTHESIS		2
#define	EXPECT_RIGHT_PARENTHESIS	4


/*
 *  read_one_word():
 *
 *  Reads the file f until it has read a complete word. Whitespace is ignored,
 *  and also any exclamation mark ('!') and anything following an exclamation
 *  mark on a line.
 *
 *  Used internally by emul_parse_config().
 */
static void read_one_word(FILE *f, char *buf, int buflen, int *line,
	int expect)
{
	int ch;
	int done = 0;
	int curlen = 0;
	int in_word = 0;

	while (!done) {
		if (curlen >= buflen - 1)
			break;

		ch = fgetc(f);
		if (ch == EOF)
			break;

		if (in_word) {
			if (is_word_char(ch)) {
				buf[curlen++] = ch;
				if (curlen == buflen - 1)
					done = 1;
				continue;
			} else {
				if (ungetc(ch, f) == EOF) {
					fatal("ungetc() failed?\n");
					exit(1);
				}
				break;
			}
		}

		if (ch == '\n') {
			(*line) ++;
			continue;
		}

		if (ch == '\r')
			continue;

		if (ch == '!') {
			/*  Skip until newline:  */
			while (ch != '\n' && ch != EOF)
				ch = fgetc(f);
			if (ch == '\n')
				(*line) ++;
			continue;
		}

		if (ch == '{') {
			/*  Skip until '}':  */
			while (ch != '}' && ch != EOF) {
				ch = fgetc(f);
				if (ch == '\n')
					(*line) ++;
			}
			continue;
		}

		/*  Whitespace?  */
		if (ch <= ' ')
			continue;

		if (ch == '"' || ch == '\'') {
			/*  This is a quoted word.  */
			int start_ch = ch;

			if (expect & EXPECT_LEFT_PARENTHESIS) {
				fatal("unexpected character '%c', line %i\n",
				    ch, *line);
				exit(1);
			}

			while (curlen < buflen - 1) {
				ch = fgetc(f);
				if (ch == '\n') {
					fatal("line %i: newline inside"
					    " quotes?\n", *line);
					exit(1);
				}
				if (ch == EOF) {
					fatal("line %i: EOF inside a quoted"
					    " string?\n", *line);
					exit(1);
				}
				if (ch == start_ch)
					break;
				buf[curlen++] = ch;
			}
			break;
		}

		if ((expect & EXPECT_WORD) && is_word_char(ch)) {
			buf[curlen++] = ch;
			in_word = 1;
			if (curlen == buflen - 1)
				done = 1;
		} else {
			if ((expect & EXPECT_LEFT_PARENTHESIS) && ch == '(') {
				buf[curlen++] = ch;
				break;
			}
			if ((expect & EXPECT_RIGHT_PARENTHESIS) && ch == ')') {
				buf[curlen++] = ch;
				break;
			}

			fatal("unexpected character '%c', line %i\n",
			    ch, *line);
			exit(1);
		}
	}

	buf[curlen] = '\0';
}


#define	PARSESTATE_NONE			0
#define	PARSESTATE_EMUL			1
#define	PARSESTATE_NET			2
#define	PARSESTATE_MACHINE		3

static char cur_net_ipv4net[50];
static char cur_net_ipv4len[50];

static char cur_machine_name[50];
static char cur_machine_cpu[50];
static char cur_machine_type[50];
static char cur_machine_subtype[50];
static char cur_machine_use_x11[10];
static char cur_machine_x11_scaledown[10];
static char cur_machine_bintrans[10];
#define	MAX_N_LOAD	20
#define	MAX_LOAD_LEN	4000
static char *cur_machine_load[MAX_N_LOAD];
static int cur_machine_n_load;

#define ACCEPT_SIMPLE_WORD(w,var) {					\
		if (strcmp(word, w) == 0) {				\
			read_one_word(f, word, maxbuflen,		\
			    line, EXPECT_LEFT_PARENTHESIS);		\
			read_one_word(f, var, sizeof(var),		\
			    line, EXPECT_WORD);				\
			read_one_word(f, word, maxbuflen,		\
			    line, EXPECT_RIGHT_PARENTHESIS);		\
			return;						\
		}							\
	}


/*
 *  parse_on_off():
 *
 *  Returns 1 for "on", "yes", "enable", or "1".
 *  Returns 0 for "off", "no", "disable", or "0".
 *  Prints a fatal warning and exit()s for other values.
 */
int parse_on_off(char *s)
{
	if (strcasecmp(s, "on") == 0 || strcasecmp(s, "yes") == 0 ||
	    strcasecmp(s, "enable") == 0 || strcasecmp(s, "1") == 0)
		return 1;
	if (strcasecmp(s, "off") == 0 || strcasecmp(s, "no") == 0 ||
	    strcasecmp(s, "disable") == 0 || strcasecmp(s, "0") == 0)
		return 0;

	fatal("parse_on_off(): unknown value '%s'\n", s);
	exit(1);
}


/*
 *  parse__none():
 *
 *  emul ( [...] )
 */
static void parse__none(struct emul *e, FILE *f, int *in_emul, int *line,
	int *parsestate, char *word, size_t maxbuflen)
{
	if (strcmp(word, "emul") == 0) {
		if (*in_emul) {
			fatal("line %i: only one emul per config "
			    "file is supported!\n", *line);
			exit(1);
		}
		*parsestate = PARSESTATE_EMUL;
		*in_emul = 1;
		read_one_word(f, word, maxbuflen,
		    line, EXPECT_LEFT_PARENTHESIS);
		return;
	}

	fatal("line %i: expecting 'emul', not '%s'\n", *line, word);
	exit(1);
}


/*
 *  parse__emul():
 *
 *  net ( [...] )
 *  machine ( [...] )
 */
static void parse__emul(struct emul *e, FILE *f, int *in_emul, int *line,
	int *parsestate, char *word, size_t maxbuflen)
{
	if (word[0] == ')') {
		*parsestate = PARSESTATE_NONE;
		return;
	}

	if (strcmp(word, "net") == 0) {
		*parsestate = PARSESTATE_NET;
		read_one_word(f, word, maxbuflen,
		    line, EXPECT_LEFT_PARENTHESIS);

		/*  Default net:  */
		strcpy(cur_net_ipv4net, "10.0.0.0");
		strcpy(cur_net_ipv4len, "8");
		return;
	}

	if (strcmp(word, "machine") == 0) {
		*parsestate = PARSESTATE_MACHINE;
		read_one_word(f, word, maxbuflen,
		    line, EXPECT_LEFT_PARENTHESIS);

		/*  A "zero state":  */
		cur_machine_name[0] = '\0';
		cur_machine_cpu[0] = '\0';
		cur_machine_type[0] = '\0';
		cur_machine_subtype[0] = '\0';
		cur_machine_n_load = 0;
		cur_machine_use_x11[0] = '\0';
		cur_machine_x11_scaledown[0] = '\0';
		cur_machine_bintrans[0] = '\0';
		return;
	}

	fatal("line %i: not expecting '%s' in an 'emul' section\n",
	    *line, word);
	exit(1);
}


/*
 *  parse__net():
 *
 *  Simple words: ipv4net, ipv4len
 *
 *  TODO: more words? for example an option to disable the gateway? that would
 *  have to be implemented correctly in src/net.c first.
 */
static void parse__net(struct emul *e, FILE *f, int *in_emul, int *line,
	int *parsestate, char *word, size_t maxbuflen)
{
	if (word[0] == ')') {
		/*  Finished with the 'net' section. Let's create the net:  */
		if (e->net != NULL) {
			fatal("line %i: more than one net isn't really "
			    "supported yet\n", *line);
			exit(1);
		}

		e->net = net_init(e, NET_INIT_FLAG_GATEWAY,
		    cur_net_ipv4net, atoi(cur_net_ipv4len));

		if (e->net == NULL) {
			fatal("line %i: fatal error: could not create"
			    " the net (?)\n", *line);
			exit(1);
		}

		*parsestate = PARSESTATE_EMUL;
		return;
	}

	ACCEPT_SIMPLE_WORD("ipv4net", cur_net_ipv4net);
	ACCEPT_SIMPLE_WORD("ipv4len", cur_net_ipv4len);

	fatal("line %i: not expecting '%s' in a 'net' section\n", *line, word);
	exit(1);
}


/*
 *  parse__machine():
 *
 *  Simple words: name, cpu, type, subtype, load, use_x11, x11_scaledown,
 *                bintrans
 *
 *  TODO: more words?
 */
static void parse__machine(struct emul *e, FILE *f, int *in_emul, int *line,
	int *parsestate, char *word, size_t maxbuflen)
{
	int r, i;

	if (word[0] == ')') {
		/*  Finished with the 'machine' section.  */
		struct machine *m;

		if (!cur_machine_name[0])
			strcpy(cur_machine_name, "no_name");

		m = emul_add_machine(e, cur_machine_name);

		r = machine_name_to_type(cur_machine_type, cur_machine_subtype,
		    &m->machine_type, &m->machine_subtype);
		if (!r)
			exit(1);

		if (cur_machine_cpu[0])
			m->cpu_name = strdup(cur_machine_cpu);

		if (!cur_machine_use_x11[0])
			strcpy(cur_machine_use_x11, "no");
		m->use_x11 = parse_on_off(cur_machine_use_x11);

		if (!cur_machine_bintrans[0])
			strcpy(cur_machine_bintrans, "no");
		m->bintrans_enable = m->bintrans_enabled_from_start =
		    parse_on_off(cur_machine_bintrans);

		if (!cur_machine_x11_scaledown[0])
			m->x11_scaledown = 1;
		else {
			m->x11_scaledown = atoi(cur_machine_x11_scaledown);
			if (m->x11_scaledown < 0) {
				fprintf(stderr, "Invalid scaledown value"
				    " (%i)\n", m->x11_scaledown);
				exit(1);
			}
		}

		emul_machine_setup(m, cur_machine_n_load,
		    cur_machine_load);

		for (i=0; i<cur_machine_n_load; i++) {
			free(cur_machine_load[i]);
			cur_machine_load[i] = NULL;
		}

		*parsestate = PARSESTATE_EMUL;
		return;
	}

	ACCEPT_SIMPLE_WORD("name", cur_machine_name);
	ACCEPT_SIMPLE_WORD("cpu", cur_machine_cpu);
	ACCEPT_SIMPLE_WORD("type", cur_machine_type);
	ACCEPT_SIMPLE_WORD("subtype", cur_machine_subtype);
	ACCEPT_SIMPLE_WORD("use_x11", cur_machine_use_x11);
	ACCEPT_SIMPLE_WORD("x11_scaledown", cur_machine_x11_scaledown);
	ACCEPT_SIMPLE_WORD("bintrans", cur_machine_bintrans);

	if (strcmp(word, "load") == 0) {
		read_one_word(f, word, maxbuflen,
		    line, EXPECT_LEFT_PARENTHESIS);
		if (cur_machine_n_load >= MAX_N_LOAD) {
			fprintf(stderr, "too many loads\n");
			exit(1);
		}
		cur_machine_load[cur_machine_n_load] = malloc(MAX_LOAD_LEN);
		if (cur_machine_load[cur_machine_n_load] == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(1);
		}
		read_one_word(f, cur_machine_load[cur_machine_n_load],
		    MAX_LOAD_LEN, line, EXPECT_WORD);
		cur_machine_n_load ++;
		read_one_word(f, word, maxbuflen,
		    line, EXPECT_RIGHT_PARENTHESIS);
		return;
	}

	fatal("line %i: not expecting '%s' in a 'machine' section\n",
	    *line, word);
	exit(1);
}


/*
 *  emul_parse_config():
 *
 *  Set up an emulation by parsing a config file.
 */
void emul_parse_config(struct emul *e, FILE *f)
{
	char word[500];
	int in_emul = 0;
	int line = 1;
	int parsestate = PARSESTATE_NONE;

	/*  debug("emul_parse_config()\n");  */

	while (!feof(f)) {
		read_one_word(f, word, sizeof(word), &line,
		    EXPECT_WORD | EXPECT_RIGHT_PARENTHESIS);
		if (!word[0])
			break;

		/*  debug("word = '%s'\n", word);  */

		switch (parsestate) {
		case PARSESTATE_NONE:
			parse__none(e, f, &in_emul, &line, &parsestate,
			    word, sizeof(word));
			break;
		case PARSESTATE_EMUL:
			parse__emul(e, f, &in_emul, &line, &parsestate,
			    word, sizeof(word));
			break;
		case PARSESTATE_NET:
			parse__net(e, f, &in_emul, &line, &parsestate,
			    word, sizeof(word));
			break;
		case PARSESTATE_MACHINE:
			parse__machine(e, f, &in_emul, &line, &parsestate,
			    word, sizeof(word));
			break;
		default:
			fatal("INTERNAL ERROR in emul_parse.c ("
			    "parsestate %i is not imlemented yet?)\n",
			    parsestate);
			exit(1);
		}
	}

	if (parsestate != PARSESTATE_NONE) {
		fatal("EOF but not enough right parentheses?\n");
		exit(1);
	}
}

