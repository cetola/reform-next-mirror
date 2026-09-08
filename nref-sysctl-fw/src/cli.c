#include "cli.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define CLI_ST_LIST 0
#define CLI_ST_WORD 1
#define CLI_ST_NUMBER 2
#define CLI_ST_HEX 3
#define CLI_ST_ERROR 5

//#define CLI_DEBUG_STATE
//#define CLI_DEBUG_EVAL

static struct cli_var cli_vars[CLI_MAX_VARS]; // known variables (named words)
static uint32_t cli_num_vars; // number of known variables

/* resets input/parser state. call cli_reset_out() to reset output state! */
void cli_reset(struct cli_context *ctx) {
  ctx->cli_word = 0;
  ctx->cli_word_len = 0;
  ctx->cli_paren = 0;
  ctx->cli_word_idx = 0;
  ctx->cli_state = CLI_ST_LIST;
}

void cli_reset_out(struct cli_context *ctx) {
  ctx->cli_err = 0;
  ctx->cli_out[0] = 0;
  ctx->cli_out_pos = 0;
}

/* returns number of chars still available in output buffer */
int cli_out_available(struct cli_context *ctx) {
  return CLI_BUFSZ - ctx->cli_out_pos;
}

char *cli_out_ptr(struct cli_context *ctx) {
  return &ctx->cli_out[ctx->cli_out_pos];
}

int cli_get_out_pos(struct cli_context *ctx) {
  return ctx->cli_out_pos;
}

char* fourcc_to_str(uint32_t word, char buf[static 5]) {
  for (int i = 0; i < 5; i++) {
    buf[i] = word&0xff;
    word >>= 8;
  }
  buf[4] = 0;
  return buf;
}

char* eightcc_to_str(uint64_t word, char buf[static 9]) {
  for (int i = 0; i < 8; i++) {
    buf[i] = word&0xff;
    word >>= 8;
  }
  buf[8] = 0;
  return buf;
}

int cli_out_str(struct cli_context* ctx, char *str) {
  uint32_t len = snprintf(cli_out_ptr(ctx), cli_out_available(ctx), "%s", str);
  if (len < strlen(str)) {
    return 0;
  }
  ctx->cli_out_pos += len;
  return len;
}

uint64_t cli_get_err(struct cli_context* ctx) {
  return ctx->cli_err;
}

void cli_error(struct cli_context* ctx, uint64_t err) {
  ctx->cli_err = err;
  char buf[5];
  ctx->cli_out_pos = snprintf(ctx->cli_out, CLI_BUFSZ, "(err %s)", fourcc_to_str(err, buf));
  cli_reset(ctx);
}

int cli_out_int64(struct cli_context* ctx, int64_t number) {
  char buf[22+6]; // (-9223372036854775808)
  snprintf(buf, sizeof(buf), "(%lld)", (long long)number);
  buf[sizeof(buf)-1] = 0;
  return cli_out_str(ctx, buf);
}

int cli_out_uint64(struct cli_context* ctx, uint64_t number) {
  char buf[22+6]; // (18446744073709551616)
  snprintf(buf, sizeof(buf), "(%llu)", (unsigned long long)number);
  buf[sizeof(buf)-1] = 0;
  return cli_out_str(ctx, buf);
}

int cli_out_type(struct cli_context* ctx, uint64_t t) {
  if (t == CLI_TYPE_FUNC) {
    return cli_out_str(ctx, "func");
  } else if (t == CLI_TYPE_UINT64) {
    return cli_out_str(ctx, "u64");
  } else if (t == CLI_TYPE_INT64) {
    return cli_out_str(ctx, "i64");
  } else if (t == CLI_TYPE_VOID) {
    return cli_out_str(ctx, "void");
  } else if (t == CLI_TYPE_STR128) {
    return cli_out_str(ctx, "str");
  } else {
    return cli_out_str(ctx, "unk");
  }
}

int cli_word_valid(uint64_t word) {
  return ((word & 0xff) >= 'a' && (word & 0xff) <= 'z') || ((word & 0xff) >= 'A' && (word & 0xff) <= 'Z');
}

uint64_t cli_num_words([[maybe_unused]] struct cli_context* ctx) {
  return cli_num_vars;
}

void cli_show_word(struct cli_context* ctx, uint64_t idx) {
  if (idx >= cli_num_vars) return;

  const struct cli_var* w = &cli_vars[idx];
  cli_out_str(ctx, "(");
  char buf[9];
  cli_out_str(ctx, eightcc_to_str(w->word, buf));
  cli_out_str(ctx, " ");
  cli_out_type(ctx, w->type);
  cli_out_str(ctx, " ");
  if (cli_vars[idx].type == CLI_TYPE_FUNC) {
    cli_out_type(ctx, w->return_type);
    cli_out_str(ctx, " ");
    cli_out_uint64(ctx, w->num_args);
  }
  cli_out_str(ctx, ")");
}

uint64_t cli_set_var_uint64(struct cli_context* ctx, uint64_t word, uint64_t value) {
#ifdef CLI_DEBUG_EVAL
  char buf[9];
  printf("[cli_set_var_uint64] %s 0x%llx\n", eightcc_to_str(word, buf), value);
#endif
  if (!cli_word_valid(word)) {
    if (ctx) cli_error(ctx, CLI_ERR_ARGS);
    return 0;
  }
  for (uint32_t i = 0; i < cli_num_vars; i++) {
    if (cli_vars[i].word == word) {
      // prevent overwriting of funcs
      // TODO: polish, might be ok for user funcs
      if (cli_vars[i].type == CLI_TYPE_FUNC) {
        if (ctx) cli_error(ctx, CLI_ERR_ARGS);
        return 0;
      }
      cli_vars[i].value_u64 = value;
      return value;
    }
  }
  if (cli_num_vars >= CLI_MAX_VARS) {
    if (ctx) cli_error(ctx, CLI_ERR_MAX_LIST);
    return 0;
  }
  cli_vars[cli_num_vars++] = (struct cli_var)
  {
    .word = word,
    .value_u64 = value,
    .type = CLI_TYPE_UINT64
  };
  return value;
}

// TODO signature!
int cli_add_func(char word[static 9], void* funcptr, uint8_t num_args, uint8_t return_type) {
  if (cli_num_vars >= CLI_MAX_VARS) {
    return 0;
  }
  // TODO overwrite existing func/var!
  cli_vars[cli_num_vars++] = (struct cli_var){
    .word = eightcc(word),
    .type = CLI_TYPE_FUNC,
    .func = funcptr,
    .num_args = num_args,
    .return_type = return_type,
  };
  return cli_num_vars;
}

int cli_add_word(char word[static 9], uint64_t value) {
  return cli_set_var_uint64(NULL, eightcc(word), value);
}

void cli_init_env() {
  cli_num_vars = 0;
  cli_add_func("words\0\0\0", cli_num_words, 0, CLI_TYPE_UINT64);
  cli_add_func("word\0\0\0\0", cli_show_word, 1, CLI_TYPE_VOID);
  cli_add_func("set\0\0\0\0\0", cli_set_var_uint64, 2, CLI_TYPE_UINT64);
}

void cli_init(struct cli_context* ctx) {
  cli_reset(ctx);
  cli_reset_out(ctx);
}

void cli_eval(struct cli_context* ctx) {
#ifdef CLI_DEBUG_EVAL
  //printf("# [cli_eval] cli_word_idx: %d\n", cli_word_idx);
#endif
  if (ctx->cli_word_idx <= 0) {
    return;
  }
  // word 0 is the operation (function pointer)
  uint64_t op = ctx->cli_list[0];
  int num_args = ctx->cli_word_idx - 1;
#ifdef CLI_DEBUG_EVAL
  char buf[9];
  printf("# [cli_eval] op: 0x%llx (%s) arity: %d\n", op, eightcc_to_str(op, buf), num_args);
#endif
  for (uint32_t i=0; i < cli_num_vars; i++) {
    struct cli_var* var = &cli_vars[i];
    //printf("# [cli_eval] candidate: 0x%llx\n", var->word);
    if (op == var->word) {
#ifdef CLI_DEBUG_EVAL
      //printf("# [cli_eval] word %d matches var table entry %d, type %d\n", cli_word_idx, i, var->type);
#endif
      if (var->type == CLI_TYPE_FUNC && var->func) {
        // maximum 4 args, all default to 0
        uint64_t args[4] = { 0, 0, 0, 0 };
        for (int j = 0; j < num_args && j < 4; j++) {
          // if an arg is passed, set it
          args[j] = ctx->cli_list[j + 1];
        }
	      // TODO DRY this
        if (var->return_type == CLI_TYPE_UINT64) {
          uint64_t (*func)(void*, uint64_t, uint64_t, uint64_t, uint64_t) = var->func;
          uint64_t result = func(ctx, args[0], args[1], args[2], args[3]);
          cli_out_uint64(ctx, result);
          return;
        } else if (var->return_type == CLI_TYPE_INT64) {
          int64_t (*func)(void*, uint64_t, uint64_t, uint64_t, uint64_t) = var->func;
          int64_t result = func(ctx, args[0], args[1], args[2], args[3]);
          cli_out_int64(ctx, result);
          return;
        } else if (var->return_type == CLI_TYPE_STR128) {
          char* (*func)(void*, uint64_t, uint64_t, uint64_t, uint64_t) = var->func;
          char* result = func(ctx, args[0], args[1], args[2], args[3]);
          cli_out_str(ctx, result);
          return;
        } else if (var->return_type == CLI_TYPE_VOID) {
          void (*func)(void*, uint64_t, uint64_t, uint64_t, uint64_t) = var->func;
          func(ctx, args[0], args[1], args[2], args[3]);
          return;
        }
        return;
      } else if (var->type == CLI_TYPE_UINT64) {
        cli_out_uint64(ctx, var->value_u64);
        return;
      } else if (var->type == CLI_TYPE_INT64) {
        cli_out_int64(ctx, var->value_i64);
        return;
      }
    }
  }
  cli_error(ctx, CLI_ERR_UNDEFINED);
}

char *cli_get_out(struct cli_context *ctx) {
  return ctx->cli_out;
}

void cli_char(struct cli_context *ctx, char c) {
#ifdef CLI_DEBUG_STATE
  printf("# [cli] s:%d c:%d w:%d\n", ctx->cli_state, c, ctx->cli_word_idx);
#endif
  if (ctx->cli_state == CLI_ST_LIST) {
    if (c == ' ') return;
    if (c == ')' || (c == '\n' && ctx->cli_paren == 0)) {
      cli_eval(ctx);
      cli_reset(ctx);
      return;
    }
    /* start a new word */
    if (ctx->cli_word_idx >= CLI_MAX_LIST_WORDS) {
      cli_error(ctx, CLI_ERR_MAX_WORD);
      return;
    }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
      ctx->cli_state = CLI_ST_WORD;
      ctx->cli_word = (uint64_t)c;
      ctx->cli_word_len = 1;
      return;
    }
    if (c >= '0' && c <= '9') {
      if (ctx->cli_word_idx == 0) {
        /* for backwards compat (old LPC API), the first word can start with a single digit */
        ctx->cli_state = CLI_ST_WORD;
        ctx->cli_word = (uint64_t)c;
        ctx->cli_word_len = 1;
      } else {
        ctx->cli_state = CLI_ST_NUMBER;
        ctx->cli_word = (uint64_t)(c-'0');
      }
      return;
    }
    if (c == '(') {
      ctx->cli_state = CLI_ST_LIST;
      ctx->cli_word_idx = 0;
      ctx->cli_paren++;
      /* TODO switch current list! */
      if (ctx->cli_paren > CLI_MAX_PAREN) {
        cli_error(ctx, CLI_ERR_MAX_PAREN);
      }
      return;
    }
    /* index 0 must be a word (callable) */
    if (ctx->cli_word_idx == 0) {
      cli_error(ctx, CLI_ERR_SYNTAX_CALL);
      return;
    }
    /* index 1+ (arguments) can be numbers or lists */
    if (c == '#') {
      ctx->cli_state = CLI_ST_HEX;
      ctx->cli_word = 0;
      return;
    }
  } else if (ctx->cli_state == CLI_ST_NUMBER) {
    if (c >= '0' && c <= '9') {
      ctx->cli_word *= 10;
      ctx->cli_word += (uint64_t)(c-'0');
      return;
    } else if (c == ' ' || c == ')' || c == '\n') {
      ctx->cli_state = CLI_ST_LIST;
      ctx->cli_list[ctx->cli_word_idx++] = ctx->cli_word;
      if (c == ')' || (c == '\n' && ctx->cli_paren == 0)) {
        cli_eval(ctx);
        cli_reset(ctx);
      }
      return;
    }
    cli_error(ctx, CLI_ERR_SYNTAX_NUM);
    return;
  } else if (ctx->cli_state == CLI_ST_WORD) {
    if (c == ' ' || c == ')' || c == '\n') {
      ctx->cli_state = CLI_ST_LIST;
#ifdef CLI_DEBUG_STATE
      //printf("# [cli_word] storing word at idx %d\n", ctx->cli_word_idx);
#endif
      ctx->cli_list[ctx->cli_word_idx++] = ctx->cli_word;
      if (c == ')' || (c == '\n' && ctx->cli_paren == 0)) {
        cli_eval(ctx);
        cli_reset(ctx);
      }
      return;
    }
    ctx->cli_word |= (uint64_t)c << (ctx->cli_word_len*8);
    ctx->cli_word_len++;
#ifdef CLI_DEBUG_STATE
    printf("# [cli_word] 0x%llx (len: %d)\n", ctx->cli_word, ctx->cli_word_len);
#endif

    if (ctx->cli_word_len > CLI_MAX_WORD_LEN) {
      cli_error(ctx, CLI_ERR_MAX_WORD);
      return;
    }
    // TODO filter illegal chars
    return;
  } else if (ctx->cli_state == CLI_ST_HEX) {
    if (c >= '0' && c <= '9') {
      ctx->cli_word = ctx->cli_word << 4;
      ctx->cli_word += (uint64_t)(c-'0');
      ctx->cli_word_len++;
      if (ctx->cli_word_len > 16) {
        cli_error(ctx, CLI_ERR_MAX_WORD);
        return;
      }
      return;
    }
    if (c >= 'a' && c <= 'f') {
      ctx->cli_word = ctx->cli_word << 4;
      ctx->cli_word += (uint64_t)(10 + (c - 'a'));
      ctx->cli_word_len++;
      if (ctx->cli_word_len > 16) {
        cli_error(ctx, CLI_ERR_MAX_WORD);
        return;
      }
      return;
    }
    if (c == ' ' || c == ')' || c == '\n') {
      ctx->cli_state = CLI_ST_LIST;
      ctx->cli_list[ctx->cli_word_idx++] = ctx->cli_word;
      if (c == ')' || (c == '\n' && ctx->cli_paren == 0)) {
        cli_eval(ctx);
        cli_reset(ctx);
        return;
      }
      return;
    }
    cli_error(ctx, CLI_ERR_SYNTAX_HEX);
    return;
  }
}
