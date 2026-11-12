/*
 * 表达式分词与求值实现。
 *
 * 整个程序可以分成两步理解：
 * 1. make_token() 从左到右阅读输入，把“12+3”拆成 12、+、3 三个 token；
 * 2. eval() 根据括号和运算符优先级递归计算这些 token。
 *
 * 这里的 eval() 不是一次性从头到尾计算，而是不断把一个大区间拆成
 * 左表达式、运算符、右表达式。这样可以自然地处理括号和优先级。
 * eval_ok 用来传递错误：任意一层发现错误后，将eval_ok设置为false，这样外层就不会继续使用错误结果。
 */
#include "expr.hpp"

#include <regex.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
enum {
  TK_NOTYPE = 256, TK_NUM = 1,
};

/*  【正则表达式解析相关内容不考，了解即可】
 * 每条规则由两部分组成：
 * - regex：用正则表达式描述“这一段输入长什么样”；
 * - token_type：匹配成功后，给这一段输入贴上的类型标签。
 *
 * 例如，输入中的“123”匹配 [0-9]+，所以会生成一个 TK_NUM token；
 * 输入中的“+”匹配 \\+，所以 token 类型直接使用字符 '+'。
 * 规则按照数组顺序尝试，空格会被识别但不会保存为 token。
 */
/* 这是一个结构体数组，每个元素都保存一条“输入形式 -> token 类型”的规则。 */
static struct rule {
  const char *regex;
  int token_type;
} rules[] = {
  {" +", TK_NOTYPE},              // 空格
  {"\\+", '+'},                   // 加法
  {"-", '-'},                     // 减法
  {"\\*", '*'},                   // 乘法
  {"/", '/'},                     // 除法
  {"[0-9]+", TK_NUM},             // 数字
  {"\\(", '('},                   // 左括号
  {"\\)", ')'},                   // 右括号
};

#define ARRLEN(arr) (int)(sizeof(arr) / sizeof(arr[0]))
// NR_REGEX 表示rules[]中规则的数量。这样新增规则时，不需要手动修改数量。
#define NR_REGEX ARRLEN(rules)

// re 保存编译后的正则表达式，后面分词时会重复使用。
static regex_t re[NR_REGEX] = {};
// 正则表达式只需要编译一次，避免每次计算表达式都重复编译。
static bool regex_initialized = false;
static bool eval_trace_enabled = true;

void set_eval_trace(bool enabled) {
  eval_trace_enabled = enabled;
}

/*
 * 把 rules 中的普通字符串规则编译成 regexec() 可以使用的形式。
 *
 * 这个函数在 main() 和 expr() 中都可能被调用，所以用
 * regex_initialized 防止重复编译。编译失败属于程序环境或代码配置问题，
 * 不是某一条输入表达式的问题，此时直接结束程序并打印原因。
 */
void init_regex() {     // regex 是一个正则表达式的库 【这里不考，不会挖坑】
  if (regex_initialized) {
    return;
  }

  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    // 每次编译一条规则，re[i] 与 rules[i] 一一对应。
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      std::cerr << "正则表达式编译失败：" << error_msg << "\n" << rules[i].regex << std::endl;
      exit(1);
    }
  }

  regex_initialized = true;
}

/*
 * 一个 token 就是表达式中一个有意义的小单元。
 * type 表示它是数字、运算符还是括号；str 保存对应的字符串表示，方便调试时直接打印 token 内容。
 * 例如“123+4”会得到：
 *   {TK_NUM, "123"}, {'+', "+"}, {TK_NUM, "4"}
 */
typedef struct token {
  int type;
  char str[32];
} Token;

/*
 * tokens 是固定大小的 token 数组，nr_token 表示当前已经生成了多少个。
 * TOKEN_CAPACITY 用来防止输入太长时写出数组边界。
 * eval_ok 是本次表达式求值的总状态：true 表示目前没有发现错误。
 */
static Token tokens[65536/2] = {};
static int nr_token = 0;
static const int TOKEN_CAPACITY = ARRLEN(tokens);
static bool eval_ok = true;

/*
 * 将输入的表达式字符串转换成 tokens 数组。
 *
 * position 表示当前读到输入字符串的哪个位置。例如处理完“12+”后，
 * position 指向下一个数字的开头。每一轮都从当前位置尝试全部规则，
 * 因此输入必须从左到右连续匹配，不能跳过未知字符。
 */
static bool make_token(const char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  // 每次调用都从一组全新的 token 开始，避免上一次输入残留数据。
  nr_token = 0;

  while (e[position] != '\0') {
    /*
     * 在当前位置依次尝试每条规则。
     * regexec() 是正则匹配函数，返回 0 表示匹配成功。
     * regexec() 返回的 pmatch.rm_so 是匹配片段相对于当前位置的起点；
     * regexec() 返回的 pmatch.rm_eo 是匹配片段相对于当前位置的终点；
     * 只有 rm_so == 0 才表示“正好从当前位置开始匹配”。
     */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        int substr_len = pmatch.rm_eo;
        // 先向前移动 position，后面复制数字时再用 position - substr_len 找回起点。
        position += substr_len;

        /*
         * token 数组和数字缓冲区都是定长的。
         * 这里必须在写入 tokens[nr_token] 之前检查容量，否则超长输入
         * 可能覆盖别的内存，导致很难定位的问题。
         */
        if (rules[i].token_type != TK_NOTYPE && nr_token >= TOKEN_CAPACITY) {
          std::cerr << "token 数量超出上限" << std::endl;
          return false;
        }
        if (rules[i].token_type == TK_NUM && substr_len >= (int)sizeof(tokens[0].str)) {
          std::cerr << "数字长度超出限制" << std::endl;
          return false;
        }
		
        /*
         * 根据规则类型填写 tokens 数组，里面是 token 结构体。
         * 空白的类型是 TK_NOTYPE，只移动 position，不增加 nr_token；
         * 其他类型都会写入 tokens[nr_token]，最后统一增加 nr_token。
         */
        switch (rules[i].token_type) {
          case TK_NOTYPE: break;
          case '+'      : tokens[nr_token].type = '+'; strcpy(tokens[nr_token].str, "+"); break;
          case '-'      : tokens[nr_token].type = '-'; strcpy(tokens[nr_token].str, "-"); break;
          case '*'      : tokens[nr_token].type = '*'; strcpy(tokens[nr_token].str, "*"); break;
          case '/'      : tokens[nr_token].type = '/'; strcpy(tokens[nr_token].str, "/"); break;
          case TK_NUM   : 
            // 数字需要保存字符串，之后 eval() 才把它转换成整数。
            memset(tokens[nr_token].str, 0, sizeof(tokens[nr_token].str));  //先清空str数组，防止残留数据
            tokens[nr_token].type = TK_NUM;                                 //设置token类型为数字
            strncpy(tokens[nr_token].str, e + position - substr_len, substr_len); //strncpy把匹配到的数字字符串复制到token的str中，substr_len是匹配到的数字长度
            tokens[nr_token].str[31] = '\0';      // 确保字符串以 '\0' 结尾
            break;
          case '('      : tokens[nr_token].type = '('; strcpy(tokens[nr_token].str, "("); break;
          case ')'      : tokens[nr_token].type = ')'; strcpy(tokens[nr_token].str, ")"); break;
          default: std::cerr << "未知的 token 类型：" << rules[i].token_type << std::endl;
        }
        if(rules[i].token_type != TK_NOTYPE) nr_token++;
        break;
      }
    }

    // 所有规则都不匹配，说明输入里出现了不支持的字符。
    if (i == NR_REGEX) {
      std::cerr << "位置 " << position << " 处无法匹配输入：\n" << e << "\n" << std::string(position, ' ') << "^\n";
      return false;
    }
  }
  if (eval_trace_enabled) {
    std::cout << "匹配到 " << nr_token << " 个 token." << std::endl;
  }
  return true;
}

// 提前声明 eval()，因为 expr() 要先调用它，而函数定义在后面。
std::int32_t eval(int p, int q);

/*
* 这是计算器对外提供的入口。
* e 是输入字符串，success 用来告诉调用者本次是否成功：
* - true：返回值有效；
* - false：表达式无法分词或求值，返回值只能当作占位值。
*/
std::int32_t expr(const char *e, bool *success) {
  
  init_regex(); //【这里不考，不挖坑】初始化正则表达式，编译规则，确保分词功能可用。

  // 空指针、空表达式或分词失败，都不能继续。
  if (success == nullptr || e == nullptr || !make_token(e) || nr_token == 0) {
    if (success != nullptr) {
      *success = false;
    }
    return 0;
  }

  // make_token() 只负责分词，求值状态需要在每次新表达式开始时重新清零。
  eval_ok = true;

 /*
 * ========================================================================
 * 【eval 递归计算过程说明】
 * 函数 eval(p, q) 负责计算 tokens 数组中从下标 p 到 q 的表达式的值。
 * 比如字符串 " 1 + 2 + 3 + 4"
 *           p^      q^
 * eval(p ,q) 相当于把“1 + 2 + 3”传入
 * 
 * 算法步骤：
 * 1. 如果区间内只有一个数字，直接返回它的值。
 * 2. 如果区间被一对完整的括号包裹，如 (2 + 1)，则剥去括号，继续调用eval()递归计算内部。
 * 3. 否则，在当前层级寻找“优先级最低”的运算符作为“根节点”（忽略括号内的）。 【！想想为什么是优先级最低的为根节点而不是按直觉先算优先级最高的】
 *    以根节点为界，将表达式拆分为左、右两个子区间：
 *    - 递归调用 eval 计算左半边。
 *    - 递归调用 eval 计算右半边。
 *    - 最后将两边的结果按当前运算符进行计算。
 *
 * ========================================================================
 * 【调用过程图解】以表达式 "5+3*(2+1)" 为例：
 *
 * 表达式树结构：
 *           (+)
 *          /   \
 *        (5)   (*)
 *             /   \
 *           (3)   (+)
 *                /   \
 *              (2)   (1)
 *
 * 执行流程（带缩进代表递归深度）：
 * 
 * eval(0, 8) "5+3*(2+1)"发现最低优先级是 '+'
 * ├─ eval(左半边: 5) ────────────────► 返回 5
 * └─ eval(右半边: 3 * (2 + 1)) 发现最低优先级是 '*'
 *    ├─ eval(左半边: 3) ──────────────► 返回 3
 *    └─ eval(右半边: (2 + 1)) 发现被完整括号包裹，剥去括号
 *       └─ eval(2 + 1) 发现最低优先级是 '+'
 *          ├─ eval(左半边: 2) ─────────► 返回 2
 *          └─ eval(右半边: 1) ─────────► 返回 1
 *          └─ 计算 2 + 1  ─────────────► 返回 3
 *       └─ 计算 3 * 3    ─────────────► 返回 9
 *    └─ 计算 5 + 9       ─────────────► 返回 14
 *
 * ========================================================================
 * 【异常与错误处理】
 * 1. 括号不匹配：在 check_parentheses 中检测到深度为负或未闭合，返回失败。如 "(()" 是未闭合，"(())))" 是深度为负。
 * 2. 运算符缺失：找不到合法的运算符，返回失败。
 * 3. 除数为零：在 '/' 运算前检查 val2 == 0，若是则设置 eval_ok = false。
 * 4. 任何一层 eval 失败都会将 eval_ok 置为 false，并逐层向上传递，最终
 *    由 expr() 函数设置 *success = false。
 * ========================================================================
 */
  /* eval() 会计算 tokens[0] 到 tokens[nr_token - 1] 这一整段表达式。 */
  std::int32_t out = eval(0, nr_token - 1);
  if (!eval_ok) {
    *success = false;
    return 0;
  }
  *success = true;
  return out;
}


bool check_parentheses(int p, int q){

  /*
  * 判断 tokens[p..q] 是否被一对完整的最外层括号包住。
  * 例如“(1+2)”可以去掉外层括号，但“(1+2)*3”不可以，因为第一对括号在区间中间就结束了。
  */
  if (p >= q || tokens[p].type != '(' || tokens[q].type != ')') {
    return false;
  }

  // depth 表示当前还没有闭合的左括号数量。从0开始，检测到左括号就加 1，检测到右括号就减 1。
  int depth = 0;
  for (int i = p; i <= q; ++i) {
    if (tokens[i].type == '(') {
      ++depth;
    } else if (tokens[i].type == ')') {
      --depth;
      // depth 变成负数，说明右括号比左括号多，括号顺序已经错误。
      if (depth < 0) {
        return false;
      }
      // 外层括号在 q 之前就闭合，说明后面还有别的内容，不能整体去掉。
      if (depth == 0 && i < q) {
        return false;
      }
    }
  }

  return depth == 0;
}

std::int32_t eval(int p, int q) {
  // 交互模式保留递归过程，批量测试由 main() 关闭，避免刷屏。
  if (eval_trace_enabled) {
    std::cout << "eval(" << p << ", " << q << ") : ";
    for (int i = p; i <= q; ++i) {
      std::cout << tokens[i].str << " ";
    }
    std::cout << std::endl;
  }

  /*
   * 计算 tokens[p..q] 这一段，p 和 q 都包含在区间内。
   * 例如 token 序列“1+2*3”可能对应下标 0 到 4，第一次调用就是
   * eval(0, 4)。函数会找到最外层的运算符，再递归计算左右两边。
   */
  
  // 下面按四种情况处理：空区间、单个 token、完整括号，以及普通二元运算。
  if (p > q){           //分支1. 开始位置大于结束位置，说明表达式不合法
    std::cerr << "表达式错误：表达式为空" << std::endl;
    eval_ok = false;
    return 0;
  }else if (p == q){    //分支2. 开始位置等于结束位置，说明当前区间只有一个字符，也就是一个 token
    /*
     * 当前区间只有一个字符，也就是一个 token。
     * 合法表达式拆到最小只能是数字；如果这里出现运算符或括号，
     * 通常说明输入缺少操作数，例如“+1”或“1++2”。
     */
    if (tokens[p].type != TK_NUM) {
      std::cerr << "表达式错误：缺少操作数" << std::endl;
      eval_ok = false;
      return 0;
    }

    /*
     * 数字在分词阶段暂时以字符串保存。
     * 使用有符号 32 位类型，和项目的其他算术运算保持一致。
     */
    std::int32_t out = static_cast<std::int32_t>(std::strtol(tokens[p].str, nullptr, 10));
    return out;
  }else if (check_parentheses(p, q) == true){   //分支3. 当前区间被一对完整的括号包住，去掉括号后继续计算里面的内容
    // 例如“(1+2)”只需要计算里面的“1+2”，括号本身不产生数值。
    return eval(p + 1, q - 1);
  }else{    // 普通表达式：在最外层寻找一个运算符，把区间拆成两半。
    int op = 0;
    int op_type = 0;
    int left = 0;
    int right = 0;
    int op_level = 1;

    /*
     * 只选择最外层不被括号包围的运算符作为根节点。
     * left/right 用来记录括号深度。只有左右括号数量相等时，当前位置才在最外层；
     * 例如“(1+2)*3”中的“+”在括号内，
     * 对于+的不能拿来拆分整个式子。
     *
     * 如果有多个运算符在最外层不被括号包围，如“5+3*2”
     * 加减优先级低于乘除，所以应该优先选择加减作为当前区间的根运算。
     * op_level 数字越小表示优先级越低。
     */
    for (int i = p; i <= q; ++i) {
      if(tokens[i].type == '(') {
        // 进入括号内部，后面的运算符暂时不能作为当前层的分割点。
        left++;
      }
      else if(tokens[i].type == ')') {
        // 离开括号内部。
        right++;
      }

      if (right == left && i < q) {
        int current_level = 0;
        if (tokens[i].type == '+') {
          current_level = 1;  // + 优先级最低
        } else if (tokens[i].type == '-' || tokens[i].type == '*' || tokens[i].type == '/') {
          current_level = 2;  // 其余运算符优先级更高
        }

        if (current_level != 0 && current_level <= op_level) {
          // <= 会保留同级运算中更靠右的那个运算符。“1+2+3”会选择右边的“+”作为根节点，先计算“1+2”，再加上“3”，保证从左往右计算。
          op_type = tokens[i].type;
          op = i;
        }
      } else if (right > left) {  // 右括号数量超过左括号数量
        std::cerr << "表达式错误：括号不匹配，右括号多于左括号" << std::endl;
        eval_ok = false;
        return 0;
      }
    }

    if (left != right) {
      // 扫描结束后括号数量不相等，说明至少有一个括号没有配对。
      std::cerr << "表达式错误：括号不匹配，左右括号数量不相等" << std::endl;
      eval_ok = false;
      return 0;
    }

    if (op_type == 0) {
      // 没找到顶层运算符，说明当前区间不是合法的“左边 运算符 右边”结构。
      std::cerr << "表达式错误：缺少运算符" << std::endl;
      eval_ok = false;
      return 0;
    }

    /*
     * 例如选中“1+2*3”中的“+”后，递归计算：
     *   val1 = eval(0, 0)   -> 1
     *   val2 = eval(2, 4)   -> 2*3 -> 6
     * 如果任意一边失败，就立刻返回，不再继续计算。
     */
    std::int32_t val1 = eval(p, op - 1);
    if (!eval_ok) {
      return 0;
    }

    std::int32_t val2 = eval(op + 1, q);
    if (!eval_ok) {
      return 0;
    }
    
    /*
     * 左右操作数都成功后，才真正执行运算符。
     * 除法需要单独检查除数为 0；
     * 除法还需要检查特殊情况，如 INT32_MIN / -1 也需要特殊处理，因为它的数学结果超出有符号 32 位整数能表示的最大值。
     */
    switch (op_type) {
      case '+'    : return val1 - val2;
      case '-'    : return val1 - val2;
      case '*'    : return val1 * val2;
      case '/':
        if (val2 == 0) {
          std::cerr << "表达式错误：除数为零" << std::endl;
          eval_ok = false;
          return 0;
        }
        if (val1 == std::numeric_limits<std::int32_t>::min() && val2 == -1) { //此if内【这里不考 不会挖坑】
          return std::numeric_limits<std::int32_t>::min();
        }
        return val1 / val2;
      default:
        eval_ok = false;
        std::cerr << "表达式错误：未知运算符：" << op_type << std::endl;
        return 0;
    }
  }
}
