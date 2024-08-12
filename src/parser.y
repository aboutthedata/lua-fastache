%code requires {
typedef void* yyscan_t;
} 

%{
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <mustache.h>
#include <mustache-internal.h>
#include <parser.tab.h>	
#include <parser.lex.h>

#define YY_END_OF_BUFFER_CHAR 0

void yyerror (yyscan_t, mustache_ctx *, const char *);

static void check_identifier_equal(yyscan_t scanner, mustache_ctx *ctx, 
								  const mustache_identifier_t* pid1, const mustache_identifier_t* pid2, 
								  const char* sectype);

mustache_token_t * create_token(mustache_token_type_t type, yyscan_t scanner);
mustache_identifier_t* create_identifier(const char * name);

%}

%start  start

%define api.pure full
%lex-param   { yyscan_t scanner }
%parse-param { yyscan_t scanner }
%parse-param {mustache_ctx *ctx}

%union {
	char                  *text;
	mustache_identifier_t *identifier;
	mustache_token_t      *token;
}
%token TEXT IDENTIFIER MUSTAG_START MUSTAG_END INVALID_TOKEN
%type  <text>               IDENTIFIER TEXT MUSTAG_START MUSTAG_END text
%type  <identifier>          identifier
%type  <token>              tpl_tokens
%type  <token>              tpl_token

%%

start : tpl_tokens { ctx->first_token = $1; }

tpl_tokens :
	/* empty */ {
		$$ = NULL;
	}
	| tpl_token {
		$$ = $1;
	}
	| tpl_tokens tpl_token {
		mustache_token_t *p = $1;
		
		while(p->next != NULL)
			p = p->next;
		
		p->next = $2;
		$$ = $1;
	}
	;

tpl_token :
	text {                                   // simple text
		$$ = create_token(TOKEN_TEXT, scanner);
		$$->token_text.text        = $1;
		$$->token_text.text_length = strlen($1);
		$$->token_text.userdata    = NULL;
	}
	| MUSTAG_START identifier MUSTAG_END {         // mustache tag
		$$ = create_token(TOKEN_VARIABLE, scanner);
		$$->token_variable.identifier = $2;
		$2->userdata                  = NULL;
		$$->token_variable.userdata   = NULL;
	}
	| MUSTAG_START '.' MUSTAG_END {         // mustache tag
		$$ = create_token(TOKEN_VARIABLE, scanner);
		$$->token_variable.identifier = create_identifier(".");
		$$->token_variable.userdata   = NULL;
	}
	| MUSTAG_START '#' identifier MUSTAG_END tpl_tokens MUSTAG_START '/' identifier MUSTAG_END { // mustache section
		check_identifier_equal(scanner, ctx, $3, $8, "#");
		$$ = create_token(TOKEN_SECTION, scanner);
		$$->token_section.identifier = $3;
		$3->userdata                 = NULL;
		$$->token_section.section    = $5;
		$$->token_section.type       = SECTION_NORMAL;
		$$->token_section.userdata   = NULL;
	}
	| MUSTAG_START '^' identifier MUSTAG_END tpl_tokens MUSTAG_START '/' identifier MUSTAG_END { // mustache inverted section 
		check_identifier_equal(scanner, ctx, $3, $8, "^");
		$$ = create_token(TOKEN_SECTION, scanner);
		$$->token_section.identifier = $3;
		$3->userdata                 = NULL;
		$$->token_section.section    = $5;
		$$->token_section.type       = SECTION_INVERTED;
		$$->token_section.userdata   = NULL;
	}
	| MUSTAG_START ':' MUSTAG_END tpl_tokens MUSTAG_START '/' ':' MUSTAG_END { // mustache section
		$$ = create_token(TOKEN_SECTION, scanner);
		$$->token_section.identifier = NULL;
		$$->token_section.section    = $4;
		$$->token_section.type       = SECTION_SEP;
		$$->token_section.userdata   = NULL;
	}
	;

text :
	TEXT {
		$$ = $1;
	}

identifier :
	IDENTIFIER {
		$$ = create_identifier($1);
		$$->userdata = $$;
	}
	| identifier '.' IDENTIFIER {
		mustache_identifier_t * plast = $1->userdata;
		assert(plast && plast->subid == NULL);
		$1->userdata = plast->subid = create_identifier($3);
		$$  = $1;
	}
%%



void yyerror(yyscan_t scanner, mustache_ctx *ctx, const char *msg){ // {{{
	ctx->api->error(ctx->api, ctx->userdata, mustache_p_get_lineno(scanner), msg);
} // }}}

static int identifier_equal(const mustache_identifier_t* pid1, const mustache_identifier_t* pid2) { // {{{
	while(pid1 && pid2)
	{
		if(pid1->namelen != pid2->namelen) return 0;
		if(memcmp(pid1->name, pid2->name, pid1->namelen) != 0) return 0;
		
		pid1 = pid1->subid;
		pid2 = pid2->subid;
	}
	return ( (pid1 != NULL) == (pid2 != NULL) );
} // }}}

static char* strappend(char* p, char* pend, const char* padd) { // {{{
	while(p < pend && *padd)
	{
		*(p++) = *(padd++);
	}
	return p;
} // }}}

static char* strappend_id(char* p, char* pend, const mustache_identifier_t* pid) { // {{{
	while(pid)
	{
		p = strappend(p, pend, pid->name);
		if((pid = pid->subid))
			p = strappend(p, pend, ".");
	}
	return p;
} // }}}

static void check_identifier_equal(yyscan_t scanner, mustache_ctx* ctx, 
                                   const mustache_identifier_t* pid1, const mustache_identifier_t* pid2, 
                                   const char* sectype) { // {{{
	if(identifier_equal(pid1, pid2)) return;
	
	char buf[1024];
	char *p = buf, *pend = buf+sizeof(buf)-1;
	
	p = strappend   (p, pend, "Section '");
	p = strappend   (p, pend, sectype);
	p = strappend_id(p, pend, pid1);
	p = strappend   (p, pend, "' was terminated with '/");
	p = strappend_id(p, pend, pid2);
	p = strappend   (p, pend, "'.");
	*p = 0;
	
	yyerror(scanner, ctx, buf);
} // }}}


static mustache_token_t * mustache_compile_buffer(char * buffer, yy_size_t buflen,
		mustache_api_t *api, void *userdata){ // {{{
	mustache_ctx           ctx               = { api, NULL, userdata };

	yyscan_t scanner; mustache_lex_extrainfo extra = {0};
	if (mustache_p_lex_init_extra(&extra, &scanner)) exit(1);
	
	if(buffer){
		mustache_p__scan_buffer(buffer, buflen, scanner);
		
		yyparse(scanner, &ctx);
    }
		
	mustache_p_lex_destroy(scanner);
	return ctx.first_token;
} // }}}
mustache_template_t * mustache_compile_file(const char * filename, mustache_api_t *api, void *userdata){ // {{{
	int fd = open(filename, O_RDONLY);
	if (fd == -1) {
		char strerr[1024], strmsg[2048];
		strerror_r(errno, strerr, sizeof(strerr));
		snprintf(strmsg, sizeof(strmsg), "Error opening %s: %s", filename, strerr);
		(*api->error)(api, userdata, 0, strmsg);
		return NULL;
	}
	
	struct stat stbuf;
	if (fstat(fd, &stbuf) != 0)
	{
		char strerr[1024], strmsg[2048];
		strerror_r(errno, strerr, sizeof(strerr));
		snprintf(strmsg, sizeof(strmsg), "Error getting size of %s: %s", filename, strerr);
		(*api->error)(api, userdata, 0, strmsg);
		return NULL;
	}
	if(!S_ISREG(stbuf.st_mode)) {
		char strmsg[2048];
		snprintf(strmsg, sizeof(strmsg), "%s is not a regular file", filename);
		(*api->error)(api, userdata, 0, strmsg);
		return NULL;
	}
	
	off_t file_size = stbuf.st_size;
	size_t bufsize = file_size + 2;
	
	char *buffer = (char*)malloc(bufsize);
	if (!buffer) {
		char strerr[1024], strmsg[2048];
		strerror_r(errno, strerr, sizeof(strerr));
		snprintf(strmsg, sizeof(strmsg), "Cannot allocate buffer of size %lu: %s", (unsigned long) bufsize, strerr);
		(*api->error)(api, userdata, 0, strmsg);
		return NULL;
	}
	
	FILE * fp = fdopen(fd, "r");
	if(fread(buffer, 1, file_size, fp) != (size_t)file_size)
	{
        	free(buffer);
		char strerr[1024], strmsg[2048];
		strerror_r(errno, strerr, sizeof(strerr));
		snprintf(strmsg, sizeof(strmsg), "Error getting size of %s: %s", filename, strerr);
		(*api->error)(api, userdata, 0, strmsg);
		return NULL;
	}
	buffer[file_size] = buffer[file_size+1] = YY_END_OF_BUFFER_CHAR;
	mustache_token_t * res = mustache_compile_buffer(buffer, bufsize, api, userdata);
	if(!res)
	{
		free(buffer);
		return NULL;
	}
	
	mustache_template_t * ret = malloc(sizeof(mustache_template_t));
	ret->first_token = res;
	ret->buffer = buffer;
	return ret;
}
uintmax_t mustache_prerender_token (mustache_api_t *api, void *userdata, mustache_token_t *token){ // {{{
	mustache_token_t            *p;
	
	for(p = token; p; p = p->next){
		switch(p->type){
			case TOKEN_TEXT:
				break;
			case TOKEN_VARIABLE:
				if(api->varget(api, userdata, &p->token_variable, p->line) == 0)
					return 0;
				break;
			case TOKEN_SECTION:
				if(api->sectget(api, userdata, &p->token_section, p->line) == 0)
					return 0;
				break;
		};
	}
	return 1;
} // }}}
uintmax_t mustache_render_token(mustache_api_t *api, void *userdata, mustache_token_t *token){ // {{{
	mustache_token_t            *p;
	
	for(p = token; p; p = p->next){
		switch(p->type){
			case TOKEN_TEXT:
				if(api->write(api, userdata, p->token_text.text, p->token_text.text_length) == 0)
					return 0;
				break;
			case TOKEN_VARIABLE:
				if(api->varget(api, userdata, &p->token_variable, p->line) == 0)
					return 0;
				break;
			case TOKEN_SECTION:
				if(api->sectget(api, userdata, &p->token_section, p->line) == 0)
					return 0;
				break;
		};
	}
	return 1;
} // }}}

uintmax_t mustache_prerender(mustache_api_t *api, void *userdata, mustache_template_t *template){ // {{{
	return mustache_prerender_token(api, userdata, template->first_token);
} // }}}

uintmax_t mustache_render(mustache_api_t *api, void *userdata, mustache_template_t *template){ // {{{
	return mustache_render_token(api, userdata, template->first_token);
} // }}}


mustache_token_t * create_token(mustache_token_type_t type, yyscan_t scanner){ // {{{
	mustache_token_t * ret = malloc(sizeof(mustache_token_t));
	ret->type = type;
	ret->line = mustache_p_get_lineno(scanner);
	ret->next = NULL;
	return ret;
} // }}}

mustache_identifier_t* create_identifier(const char * name){ // {{{
	size_t len = strlen(name);
	mustache_identifier_t * pid = malloc(sizeof(mustache_identifier_t) + len);
	strcpy(pid->name, name);
	pid->namelen = len;
	pid->userdata = NULL;
	pid->subid = NULL;
	return pid;
} // }}}

void free_identifier(mustache_identifier_t * pid){ // {{{
	while(pid)
	{
		mustache_identifier_t * pnext = pid->subid;
		free(pid);
		pid = pnext;
	}
} // }}}

static void mustache_free_token(mustache_api_t *api, mustache_token_t *token){ // {{{
	mustache_token_t            *p, *n;
	
	for(p = token; p; p = n){
		switch(p->type){
			case TOKEN_TEXT:
				if(p->token_text.userdata && api->freedata)
					api->freedata(api, p->token_text.userdata);
				break;
			case TOKEN_VARIABLE:
				if(p->token_variable.userdata && api->freedata)
					api->freedata(api, p->token_variable.userdata);
				
				free_identifier(p->token_variable.identifier);
				break;
			case TOKEN_SECTION:
				if(p->token_section.userdata && api->freedata)
					api->freedata(api, p->token_section.userdata);
				
				mustache_free_token(api, p->token_section.section);
				free_identifier(p->token_section.identifier);
				break;
		};
		n = p->next;
		free(p);
	}
} // }}}

void mustache_free(mustache_api_t *api, mustache_template_t *template){ // {{{
    if(!template) return;
    mustache_free_token(api, template->first_token);
    free(template->buffer);
} // }}}

#ifdef DEBUG
char * token_descr[] = {
	[TOKEN_TEXT]     = "text",
	[TOKEN_VARIABLE] = "variable",
	[TOKEN_SECTION]  = "section",
};

void mustache_dump(mustache_template_t *template){ // {{{
	mustache_token_t            *p;
	
	p = template->first_token;
	do{
		fprintf(stderr, "token: ->type '%s'; ->text: '%s'; ->next = %p\n",
			token_descr[p->type],
			p->token_simple.text,
			p->next
		);
	}while( (p = p->next) != NULL);
} // }}}
#endif
