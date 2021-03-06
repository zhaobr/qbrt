
%top {
#include "qbtoken.h"
#include "qbparse.h"
#include <iostream>
#include <sstream>

std::queue< Token * > lexqueue;
int32_t Token::s_lineno = 1;
int32_t Token::s_column = 1;

#define queue_token(t) do { \
	lexqueue.push(new Token(t, yytext)); \
} while (0)

#define return_token(t) do { \
	lexqueue.push(new Token(t, yytext)); \
	return t; \
} while (0)

using namespace std;

ostringstream g_strlit;
}



DIGIT	[0-9]
ALPHA	[a-zA-Z]
NAMEC	[a-zA-Z0-9_]
STRC	(\\.|[^\"])

ID	[a-z_]{NAMEC}*

VAR	\${ID}+
ARG	%{DIGIT}+
REG	\${DIGIT}+
SUBIDX	\.{DIGIT}+
SUBNAME	\.{ID}+

LABEL	@{NAMEC}+
HASHTAG	#{NAMEC}+
INT	\-?{DIGIT}+

MODNAME	({ID}\/)*
TYPENAME	[A-Z]{NAMEC}*
TYPEVAR	\*[A-Z]{NAMEC}*

%x ARGS
%x STR
%x COMMENT

%option noyywrap
%option outfile="lib/qblex.c"

%%

<ARGS>"\""	{
	g_strlit.str("");
	++Token::s_column;
	BEGIN(STR);
}

<ARGS>{ARG} {
	return_token(TOKEN_ARG);
}
<ARGS>{VAR} {
	return_token(TOKEN_VAR);
}
<ARGS>{REG} {
	return_token(TOKEN_REG);
}
<ARGS>"\\result" {
	return_token(TOKEN_RESULT);
}

<ARGS>{SUBNAME} {
	return_token(TOKEN_SUBNAME);
}
<ARGS>{SUBIDX} {
	return_token(TOKEN_SUBIDX);
}

<ARGS>"\\void"		{
		return_token(TOKEN_VOID);
	}
<ARGS>"\\pid"	{ return_token(TOKEN_PID); }

<ARGS>{LABEL}		{
		return_token(TOKEN_LABEL);
	}

<ARGS>{HASHTAG}	{
		return_token(TOKEN_HASHTAG);
	}

<ARGS>{INT}	{
		return_token(TOKEN_INT);
	}

"bind"		{ BEGIN(ARGS); return_token(TOKEN_BIND); }
"bindtype"	{ BEGIN(ARGS); return_token(TOKEN_BINDTYPE); }
"call"		{ BEGIN(ARGS); return_token(TOKEN_CALL); }
"cfailure"	{ BEGIN(ARGS); return_token(TOKEN_CFAILURE); }
"cmp="		{ BEGIN(ARGS); return_token(TOKEN_CMP_EQ); }
"cmp!="		{ BEGIN(ARGS); return_token(TOKEN_CMP_NOTEQ); }
"cmp<"		{ BEGIN(ARGS); return_token(TOKEN_CMP_LT); }
"cmp<="		{ BEGIN(ARGS); return_token(TOKEN_CMP_LTEQ); }
"cmp>"		{ BEGIN(ARGS); return_token(TOKEN_CMP_GT); }
"cmp>="		{ BEGIN(ARGS); return_token(TOKEN_CMP_GTEQ); }
"const"		{ BEGIN(ARGS); return_token(TOKEN_CONST); }
"construct"	{ BEGIN(ARGS); return_token(TOKEN_CONSTRUCT); }
"copy"		{ BEGIN(ARGS); return_token(TOKEN_COPY); }
"ctuple"	{ BEGIN(ARGS); return_token(TOKEN_CTUPLE); }
"datatype"	{ BEGIN(ARGS); return_token(TOKEN_DATATYPE); }
"dparam"	{ BEGIN(ARGS); return_token(TOKEN_DPARAM); }
"fieldget"	{ BEGIN(ARGS); return_token(TOKEN_FIELDGET); }
"fieldset"	{ BEGIN(ARGS); return_token(TOKEN_FIELDSET); }
"fork"		{ BEGIN(ARGS); return_token(TOKEN_FORK); }
"func"		{ BEGIN(ARGS); return_token(TOKEN_FUNC); }
"goto"		{ BEGIN(ARGS); return_token(TOKEN_GOTO); }
"if"		{ BEGIN(ARGS); return_token(TOKEN_IF); }
"ifnot"		{ BEGIN(ARGS); return_token(TOKEN_IFNOT); }
"iffail"	{ BEGIN(ARGS); return_token(TOKEN_IFFAIL); }
"ifnotfail"	{ BEGIN(ARGS); return_token(TOKEN_IFNOTFAIL); }
"iadd"		{ BEGIN(ARGS); return_token(TOKEN_IADD); }
"idiv"		{ BEGIN(ARGS); return_token(TOKEN_IDIV); }
"imult"		{ BEGIN(ARGS); return_token(TOKEN_IMULT); }
"isub"		{ BEGIN(ARGS); return_token(TOKEN_ISUB); }
"lcontext"	{ BEGIN(ARGS); return_token(TOKEN_LCONTEXT); }
"lconstruct"	{ BEGIN(ARGS); return_token(TOKEN_LCONSTRUCT); }
"lfunc"		{ BEGIN(ARGS); return_token(TOKEN_LFUNC); }
"match"		{ BEGIN(ARGS); return_token(TOKEN_MATCH); }
"matchargs"	{ BEGIN(ARGS); return_token(TOKEN_MATCHARGS); }
"module"	{ BEGIN(ARGS); return_token(TOKEN_MODULE); }
"newproc"	{ BEGIN(ARGS); return_token(TOKEN_NEWPROC); }
"patternvar"	{ BEGIN(ARGS); return_token(TOKEN_PATTERNVAR); }
"protocol"	{ BEGIN(ARGS); return_token(TOKEN_PROTOCOL); }
"abstract"	{ BEGIN(ARGS); return_token(TOKEN_ABSTRACT); }
"recv"		{ BEGIN(ARGS); return_token(TOKEN_RECV); }
"stracc"	{ BEGIN(ARGS); return_token(TOKEN_STRACC); }
"wait"		{ BEGIN(ARGS); return_token(TOKEN_WAIT); }
"noop"		{
		printf("noop token\n");
		return_token(TOKEN_NOOP);
	}
"ref"		{ BEGIN(ARGS); return_token(TOKEN_REF); }
"return"	{ return_token(TOKEN_RETURN); }


"end."		{ return_token(TOKEN_END); }
{LABEL}		{ return_token(TOKEN_LABEL); }
"##"	{
	Token::s_column += yyleng; // start the comment
	BEGIN(COMMENT);
}
[ \t]	{
	++Token::s_column; // eat whitespace
}

<ARGS>".\/"		{
	return_token(TOKEN_CURRENTMOD);
}
<ARGS>{MODNAME}	{
		return_token(TOKEN_MODNAME);
	}
<ARGS>{ID}	{
		return_token(TOKEN_ID);
	}
<ARGS>{TYPENAME}	{
		return_token(TOKEN_TYPENAME);
	}
<ARGS>{TYPEVAR}	{
		return_token(TOKEN_TYPEVAR);
	}


<ARGS>"("	{ return_token(TOKEN_LPAREN); }
<ARGS>","	{ return_token(TOKEN_COMMA); }
<ARGS>")"	{ return_token(TOKEN_RPAREN); }
<ARGS>[ \t]	{
	/* eat whitespace */
	++Token::s_column;
}
<ARGS>"##"	{
	Token::s_column += yyleng; // start an end of line comment
	BEGIN(COMMENT);
}

<ARGS>.	{
	printf("Unrecognized arg character:\t'%s' %d @ ", yytext, *yytext);
	printf("%d,%d\n", Token::s_lineno, Token::s_column);
	exit(1);
}

.	{
	printf("Unrecognized keyword character:\t'%s' %d @ ", yytext, *yytext);
	printf("%d,%d\n", Token::s_lineno, Token::s_column);
	exit(1);
}


<STR>"\""	{
	lexqueue.push(new Token(TOKEN_STR, g_strlit.str()));
	++Token::s_column;
	BEGIN(INITIAL);
	return TOKEN_STR;
}
<STR>"\\n"	{
	g_strlit << "\n";
	Token::s_column += 2;
}
<STR>"\\\\"	{
	g_strlit << "\\";
	Token::s_column += 2;
}
<STR>[^\"\\]+ {
	g_strlit << yytext;
	Token::s_column += yyleng;
}

<STR>.	{
	cerr << "Unrecognized string char:\t" << yytext << endl;
	exit(1);
}

<COMMENT>.*	{
	Token::s_column += yyleng;
}

%%
