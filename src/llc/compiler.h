#ifndef LLC_COMPILER_H
#define LLC_COMPILER_H

#include <llc/tokenizer.h>
#include <llc/parser.h>

namespace llc {

struct Compiler {
    template <typename F>
    void bind(std::string name, F func) {
        parser.bind(name, func);
    }

    template <typename T>
    auto bind(std::string name) {
        return parser.bind<T>(name);
    }

    Program compile(std::string source) {
        auto tokens = tokenizer.tokenize(source);
        return parser.parse(source, tokens);
    }

  private:
    Tokenizer tokenizer;
    Parser parser;
};

}  // namespace llc

#endif  // LLC_COMPILER_H