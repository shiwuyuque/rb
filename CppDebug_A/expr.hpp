#ifndef EXPR_HPP
#define EXPR_HPP

#include <cstdint>

void init_regex(void);
void set_eval_trace(bool enabled);
std::int32_t expr(const char *expression, bool *success);

#endif
