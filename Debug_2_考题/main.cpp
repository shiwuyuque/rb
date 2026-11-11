#include "expr.hpp"

#include <fstream>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

/* 比较测试文件中每个非空行的“期望值 表达式”与计算器结果。
 * 手写测试数据和随机生成数据都通过这个函数检查。 */
static int run_test_file(const char *path) {
    std::ifstream input(path);
    if (!input) {
        std::cerr << "错误：无法打开测试文件：" << path << std::endl;
        return 1;
    }

    std::string line;
    int line_number = 0;
    int tested = 0;
    int passed = 0;

    while (std::getline(input, line)) {
        ++line_number;
        if (line.find_first_not_of(" \t\r") == std::string::npos) {
            continue;
        }

        // 先读取期望值，剩余部分作为表达式，从而保留表达式内部的空格。
        std::istringstream parser(line);
        std::int32_t expected = 0;
        std::string expression;
        if (!(parser >> expected) || !std::getline(parser, expression)) {
            std::cout << "第 " << line_number << " 行：测试格式无效" << std::endl;
            continue;
        }

        const std::string whitespace = " \t\r";
        const std::string::size_type first = expression.find_first_not_of(whitespace);
        if (first == std::string::npos) {
            std::cout << "第 " << line_number << " 行：表达式无效" << std::endl;
            continue;
        }
        expression.erase(0, first);

        bool success = false;
        const std::int32_t actual = expr(expression.c_str(), &success);
        ++tested;

        const bool result_matches = actual == expected;
        if (success && result_matches) {
            ++passed;
        } else {
            std::cout << "FAIL | line: " << line_number
                      << " | expression: " << expression
                      << " | expected: " << expected << " | actual: ";
            if (success) {
                std::cout << actual << std::endl;
            } else {
                std::cout << "invalid expression" << std::endl;
            }
        }
    }

    std::cout << "Summary: " << passed << "/" << tested << " passed" << std::endl;
    if (passed == tested) {
        std::cout << "All tests passed!" << std::endl;
    } else {
        std::cout << "部分测试失败。" << std::endl;
    }
    return passed == tested ? 0 : 1;
}

int main(int argc, char *argv[]) {
    if (argc > 2) {
        std::cerr << "用法：" << argv[0] << " [测试文件]" << std::endl;
        return 1;
    }

    // 在接收输入前先编译一次公共分词规则。
    init_regex();

    if (argc == 2) {
        set_eval_trace(false);
        return run_test_file(argv[1]);
    }

    set_eval_trace(true);

    std::string expression;
    bool success = false;

    std::cout << "Please input the expression: ";
    if (!std::getline(std::cin, expression)) {
        return 0;
    }

    const std::int32_t result = expr(expression.c_str(), &success);
    if (success) {
        std::cout << result << std::endl;
        return 0;
    }

    std::cout << "错误：表达式无效" << std::endl;
    return 1;
}
