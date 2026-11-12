#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

// 固定缓冲区足够应付这个小型考试辅助程序，也能避免引入动态内存管理。
int buf_index = 0;
int signedbuf_index = 0;
static char buf[32768] = {'\0'};
static char signedbuf[65536] = {'\0'};
static char code_buf[65536 + 1280] = {0}; // 比 buf 略大，容纳插入的类型转换
static char *code_format =
"#include <stdint.h>\n"
"#include <inttypes.h>\n"
"#include <stdio.h>\n"
"#include <stdlib.h>\n"
"#include <signal.h>\n"
"#include <setjmp.h>\n"
"static jmp_buf env;\n"
"static int32_t runtime_num(int32_t value) {\n"
"    volatile int32_t runtime_value = value;\n"
"    return runtime_value;\n"
"}\n"
"void handle_div_zero(int sig) {\n"
"    longjmp(env, 1);\n"
"}\n"
"__attribute__((constructor)) void init() {\n"
"    signal(SIGFPE, handle_div_zero);\n"
"}\n"
"int main() {\n"
"    if (setjmp(env) == 0) {\n"
"        int32_t result = %s;\n"
"        printf(\"%%\" PRId32, result);\n"
"    } else {\n"
"        printf(\"xxxxxxxxxx\");\n"
"    }\n"
"    return 0;\n"
"}";

int32_t choose(int32_t n){
  return (int32_t)(rand() % n);
}
void gen_num(){
  int n=choose(3);
  for(int i=0;i<=n;i++){
    int k;
    if(i==0)
      k=1+choose(9);
    else
      k=choose(10);
    buf[buf_index]=(char)(k+'0');
    buf_index++;
  }
}
void gen(char c){
  buf[buf_index]=c;
  buf_index++;
}
void gen_rand_op(){
  switch (choose(4))
  {
  case 0:buf[buf_index]='+';break;
  case 1:buf[buf_index]='-';break;
  case 2:buf[buf_index]='*';break;
  default:buf[buf_index]='/';break;
  }
  buf_index++;
}

static void gen_rand_expr(int depth){
  // 限制括号嵌套深度，保证生成的程序易于编译，同时覆盖递归括号和优先级。
  if(depth>10){
    gen_num();
  }else{
    if(choose(2)) gen(' ');
    switch (choose(3)) {
      case 0: gen_num(); break;
      case 1: gen('('); gen_rand_expr(depth+1); gen(')'); break;
      default: gen_rand_expr(depth+1); gen_rand_op(depth+1); gen_rand_expr(depth+1); break;
    }
  }
}

int main(int argc, char *argv[]) {
  int seed = time(0);
  srand(seed);
  int loop = 1;
  if (argc > 1) {
    sscanf(argv[1], "%d", &loop);
  }
  
  for (int i = 0; i < loop; i ++) {
    // 每条表达式都重新生成参考程序，让编译器按有符号 32 位规则计算期望值。
    memset(buf, 0, sizeof(buf));
    memset(signedbuf, 0, sizeof(signedbuf));
    buf_index = 0;
    signedbuf_index = 0;
    gen_rand_expr(0);
    // Force every literal through a volatile read so constant division by zero
    // is checked at runtime instead of being folded by the compiler.
    for(int i=0;i<buf_index;i++){
      const int is_digit = buf[i] >= '0' && buf[i] <= '9';
      const int starts_number = is_digit &&
          (i == 0 || buf[i - 1] < '0' || buf[i - 1] > '9');
      const int ends_number = is_digit &&
          (i + 1 == buf_index || buf[i + 1] < '0' || buf[i + 1] > '9');
      if (starts_number) {
        memcpy(signedbuf + signedbuf_index, "runtime_num(", 12);
        signedbuf_index += 12;
      }
      signedbuf[signedbuf_index++] = buf[i];
      if (ends_number) {
        signedbuf[signedbuf_index++] = ')';
      }
    }
    sprintf(code_buf, code_format, signedbuf);

    // Use a per-case temporary source file so concurrent generators cannot
    // overwrite each other's reference program.
    char code_path[] = "/tmp/vanguard-expr-code-XXXXXX";
    const int code_fd = mkstemp(code_path);
    if (code_fd == -1) {
      perror("无法创建临时源码文件");
      return 1;
    }

    FILE *fp = fdopen(code_fd, "w");
    if (fp == NULL) {
      close(code_fd);
      unlink(code_path);
      perror("无法打开临时源码文件");
      return 1;
    }
    fputs(code_buf, fp);
    fclose(fp);

    char expr_path[sizeof(code_path) + 8];
    snprintf(expr_path, sizeof(expr_path), "%s.bin", code_path);
    char compile_command[256];
    snprintf(compile_command, sizeof(compile_command),
             "gcc -x c -std=c11 -fwrapv -w '%s' -o '%s'", code_path, expr_path);
    int ret = system(compile_command);
    if (ret != 0){
      unlink(code_path);
      unlink(expr_path);
      i--;
      continue;
    }

    char run_command[sizeof(expr_path) + 3];
    snprintf(run_command, sizeof(run_command), "'%s'", expr_path);
    fp = popen(run_command, "r");
    if(fp == NULL){
      unlink(code_path);
      unlink(expr_path);
      i--;
      fprintf(stderr,"无法打开生成的测试程序\n");
      continue;
    }
    int32_t result;
    ret = fscanf(fp, "%" SCNd32, &result);
    if(ret<=0){
      i--;
      pclose(fp);
      unlink(code_path);
      unlink(expr_path);
      continue;
    }else{
      printf("%" PRId32 " %s\n", result, buf);
    }   
    pclose(fp);
    unlink(code_path);
    unlink(expr_path);
  }

  return 0;
}
