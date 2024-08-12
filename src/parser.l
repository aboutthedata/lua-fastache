%{
#include <stdint.h>
#include <stdio.h>
#include "mustache.h"
#include "mustache-internal.h"
#include "parser.tab.h"
%}

%option reentrant bison-bridge
%option noyywrap
%option case-insensitive
%option noinput
%option nounput
%option yylineno
%option extra-type="mustache_lex_extrainfo*"

%x comment
%x mustag

name                   [a-z0-9][a-z0-9_]*
special                [#^/\.:]
mustagopen             "«"
notmustagopen          [^\xc2]|(\xc2[^\xab])
                       /* notmustagopen means anything other than the mustagopen string. 
                       The UTF8 representation of « is 0xc2, 0xab. The combination 0xc2, 0xc2 does not exist in UTF8. */
mustagclose            "»"

%%

<comment>{mustagopen}   { ++yyextra->commentlevel; }
<comment>{mustagclose}   { if(--yyextra->commentlevel == 0) {BEGIN(INITIAL);} }
<comment>(.|"\n")

<mustag>[ \t\n]
<mustag>{special}        { return *yytext; }
<mustag>{name}           { yylval->text = yytext; return IDENTIFIER; }
<mustag>{mustagclose}    { BEGIN(INITIAL); return MUSTAG_END;  }
<mustag>.                { fprintf(stderr, "Invalid token: '%s'\n", yytext); return INVALID_TOKEN; }

{mustagopen}"!"          { yyextra->commentlevel = 1; BEGIN(comment); }
{mustagopen}             { BEGIN(mustag); return MUSTAG_START; }

{notmustagopen}*         { yylval->text = yytext; return TEXT; }

%%

