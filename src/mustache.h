#ifndef MUSTACHE_H
#define MUSTACHE_H

#include <stdint.h>

/** @file mustache.h */

typedef struct mustache_ctx               mustache_ctx;
typedef struct mustache_api_t             mustache_api_t;                ///< Api typedef
typedef struct mustache_token_t           mustache_token_t;              ///< Token typedef
typedef struct mustache_template_t        mustache_template_t;           ///< Template typedef
typedef struct mustache_identifier_t      mustache_identifier_t;         ///< Token variable typedef
typedef struct mustache_token_text_t      mustache_token_text_t;         ///< Token variable typedef
typedef struct mustache_token_variable_t  mustache_token_variable_t;     ///< Token variable typedef
typedef struct mustache_token_section_t   mustache_token_section_t;      ///< Token section typedef

/** Read callback, used only on compilation stage. Write your data in buffer and return number of bytes read.
 * @param  api         Current api set, probably not used in this callback
 * @param  userdata    Userdata passed to mustache_compile function
 * @param  buffer      Output buffer
 * @param  buffer_size Output buffer size
 * @retval 0      No more data (EOF)
 * @retval >0     Successful call, number indicate number of bytes read
 */
typedef uintmax_t (*mustache_api_read)   (mustache_api_t *api, void *userdata, char *buffer, uintmax_t buffer_size);

/** Write callback. Save input data in desired place.
 * @param  api         Current api set
 * @param  userdata    Userdata passed to mustache_render function
 * @param  buffer      Input buffer
 * @param  buffer_size Input buffer size
 * @retval 0      Error occured
 * @retval >0     Successful call
 */
typedef uintmax_t (*mustache_api_write)  (mustache_api_t *api, void *userdata, char const *buffer, uintmax_t buffer_size);

/** Get variable callback. User call ->write api to dump variable value to output, or do nothing.
 * @param  api         Current api set
 * @param  userdata    Userdata passed to mustache_render function
 * @param  token       Mustache variable token
 * @param  lineno      Line number of the token, for user feedback purposes
 * @retval 0      Error occured
 * @retval >0     Successful call
 */
typedef uintmax_t (*mustache_api_varget) (mustache_api_t *api, void *userdata, mustache_token_variable_t *token, uintmax_t lineno); 

/** Get section callback. User call iterativly render_token routines on section, or do nothing.
 * @param  api         Current api set
 * @param  userdata    Userdata passed to mustache_render function
 * @param  token       Mustache section token
 * @param  lineno      Line number of the token, for user feedback purposes
 * @retval 0      Error occured
 * @retval >0     Successful call
 */
typedef uintmax_t (*mustache_api_sectget)(mustache_api_t *api, void *userdata, mustache_token_section_t  *token, uintmax_t lineno); 

/** Errors callback. Used only on compilation stage.
 * @param  api         Current api set
 * @param  userdata    Userdata passed to mustache_compile function
 * @param  lineno      Line number on which error occured
 * @param  error       Error description
 */
typedef void      (*mustache_api_error)  (mustache_api_t *api, void *userdata, uintmax_t lineno, char const *error);

/** Free userdata callback.
 * @param  api         Current api set
 * @param  userdata    Userdata saved in tokens
 */
typedef void      (*mustache_api_freedata)(mustache_api_t *api, void *userdata);

typedef struct
{
    int commentlevel;
} mustache_lex_extrainfo;

/** Token type enum */
typedef enum mustache_token_type_t {
	TOKEN_TEXT,                             ///< Type text
	TOKEN_VARIABLE,                         ///< Type variable
	TOKEN_SECTION                           ///< Type section
} mustache_token_type_t;

typedef enum {
	SECTION_NORMAL,                         ///< Normal section, i.e. iterate over list or execute on true
	SECTION_INVERTED,                       ///< Inverted section, i.e. execute on false
	SECTION_SEP                             ///< Separator section, i.e. print if parent iteration not last item
} mustache_sec_type_t;

struct mustache_token_text_t {
	char                  *text;            ///< Text or variable name
	uintmax_t              text_length;     ///< Text length or variable name length
	void                  *userdata;        ///< Userdata
};

struct mustache_identifier_t {
	uintmax_t              namelen;         ///< Variable name length
	void                  *userdata;        ///< Userdata
	mustache_identifier_t  *subid;           ///< Sub-Identifier (in constructions like obj.sub)
	char                   name[1];         ///< Variable name
};

struct mustache_token_variable_t {
	mustache_identifier_t *identifier;      ///< Identifier (i.e. name) of the variable
	void                  *userdata;        ///< Userdata
};

struct mustache_token_section_t {
	mustache_identifier_t  *identifier;        ///< Identifier (i.e. name) of the section
	mustache_token_t      *section;         ///< Section template
	mustache_sec_type_t    type;            ///< Type of the section (normal, inverted, separator, ...)
	void                  *userdata;        ///< Userdata
};

struct mustache_token_t {
	mustache_token_type_t  type;            ///< Token type

	union {
		mustache_token_text_t         token_text;
		mustache_token_variable_t     token_variable;
		mustache_token_section_t      token_section;
	};

    
	uintmax_t line;
	mustache_token_t      *next;            ///< Next token in chain
};

struct mustache_template_t {
	mustache_token_t      *first_token;     ///< First token (begin of chain)
	char                  *buffer;          ///< Parse buffer (contains string literals)
};


struct mustache_api_t {
	mustache_api_read     read;             ///< Read callback
	mustache_api_write    write;            ///< Write callback
	mustache_api_varget   varget;           ///< Get variable callback
	mustache_api_sectget  sectget;          ///< Get section callback
	mustache_api_error    error;            ///< Error callback
	mustache_api_freedata freedata;         ///< Free userdata callback
};

// api

/** Compile template from file
 * @param  filename     Path of the file to read the template from
 * @param  api         Current api set
 * @param  userdata    Userdata passed to ->read function
 * @retval NULL  Error occured
 * @retval !NULL Successful call
 */
mustache_template_t * mustache_compile_file(const char * filename, mustache_api_t *api, void *userdata);

/** Prerender template. You will receive ->varget and ->sectget callbacks only, precompute some info if needed and save to userdata field.
 * @param  api         Current api set
 * @param  userdata    Userdata passed to ->read function
 * @param  template    Template to use for rendering
 * @retval 0  Error occured
 * @retval >0 Successful call
 */
uintmax_t             mustache_prerender(mustache_api_t *api, void *userdata, mustache_template_t *template);

/** Render template
 * @param  api         Current api set
 * @param  userdata    Userdata passed to ->write and other functions
 * @param  template    Template to use for rendering
 * @retval 0  Error occured
 * @retval >0 Successful call
 */
uintmax_t             mustache_render(mustache_api_t *api, void *userdata, mustache_template_t *template);

/** Prerender part of a template given by token. Cf. \ref mustache_prerender
 * @param  api         Current api set
 * @param  userdata    Userdata passed to ->read function
 * @param  template    Template to use for rendering
 * @retval 0  Error occured
 * @retval >0 Successful call
 */
uintmax_t             mustache_prerender_token(mustache_api_t *api, void *userdata, mustache_token_t *token);

/** Render template. Cf. \ref mustache_render
 * @param  api         Current api set
 * @param  userdata    Userdata passed to ->write and other functions
 * @param  template    Template to use for rendering
 * @retval 0  Error occured
 * @retval >0 Successful call
 */
uintmax_t             mustache_render_token(mustache_api_t *api, void *userdata, mustache_token_t *token);


/** Free template
 * @param  template    Template to free
 */
void                  mustache_free   (mustache_api_t *api, mustache_template_t *template);

// helpers (build with --enable-helpers, default)

/** Helper context */
typedef struct mustache_str_ctx {
	char                  *string;       ///< String to read or write

	uintmax_t              offset;       ///< Internal data. Current string offset.
} mustache_str_ctx;

// debug api (build with --enable-debug, not default)
void                  mustache_dump   (mustache_template_t *template); ///< Debug dump template

#endif
