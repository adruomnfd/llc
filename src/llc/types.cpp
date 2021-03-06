#include <llc/types.h>

#include <algorithm>

namespace llc {

std::unordered_map<size_t, std::string> type_id_to_name = {
    {typeid(void).hash_code(), "void"},           {typeid(int).hash_code(), "int"},
    {typeid(char).hash_code(), "char"},          {typeid(uint8_t).hash_code(), "uint8_t"},
    {typeid(uint16_t).hash_code(), "uint16_t"},  {typeid(uint32_t).hash_code(), "uint32_t"},
    {typeid(uint64_t).hash_code(), "uint64_t"},  {typeid(int8_t).hash_code(), "int8_t"},
    {typeid(int16_t).hash_code(), "int16_t"},    {typeid(int64_t).hash_code(), "int64_t"},
    {typeid(float).hash_code(), "float"},        {typeid(double).hash_code(), "double"},
    {typeid(std::string).hash_code(), "string"}, {typeid(bool).hash_code(), "bool"},
};

std::string Location::operator()(const std::string& source) const {
    LLC_CHECK(line >= 0);
    LLC_CHECK(column >= 0);
    LLC_CHECK(length > 0);
    std::vector<std::string> lines = separate_lines(source);
    LLC_CHECK(column + length <= (int)lines[line].size());

    std::string location = std::to_string(line) + ':' + std::to_string(column) + ':';
    if (filepath != "")
        location = filepath + ':' + location;

    std::string raw = lines[line];
    std::string underline(raw.size(), ' ');

    for (int i = 0; i < length; i++)
        underline[column + i] = '~';

    return location + '\n' + raw + '\n' + underline;
}

std::string enum_to_string(TokenType type) {
    static const char* map[] = {
        "number", "++", "--", "+",          "-",       "*",    "/",         "(",  ")",
        "{",      "}",  ";",  "identifier", ".",       ",",    "<",         "<=", ">",
        ">=",     "==", "!=", "=",          "!",       "char", "string",    "[",  "]",
        "+=",     "-=", "*=", "/=",         "invalid", "eof",  "num_tokens"};
    std::string str;
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if ((uint64_t(type) >> i) & 1ul)
            str += (std::string)map[i] + "|";

    if (str.back() == '|')
        str.pop_back();
    return str;
}

Object& BaseObject::get_member(const std::string& name) {
    if (members.find(name) == members.end())
        throw_exception("cannot find member \"", name, '"');
    return members[name];
}

Scope::Scope() {
    types["void"] = Object();
    types["int"] = Object(int(0));
    types["char"] = Object(char(0));
    types["uint8_t"] = Object(uint8_t(0));
    types["uint16_t"] = Object(uint16_t(0));
    types["uint32_t"] = Object(uint32_t(0));
    types["uint64_t"] = Object(uint64_t(0));
    types["int8_t"] = Object(int8_t(0));
    types["int16_t"] = Object(int16_t(0));
    types["int64_t"] = Object(int64_t(0));
    types["float"] = Object(0.0f);
    types["double"] = Object(0.0);
    types["bool"] = Object(false);
}
std::optional<Object> Scope::run(const Scope&) const {
    for (const auto& statement : statements)
        LLC_CHECK(statement != nullptr);

    for (const auto& statement : statements) {
        statement->run(*this);
    }

    return std::nullopt;
}
std::optional<Object> Scope::find_type(const std::string& name) const {
    auto it = types.find(name);
    if (it == types.end())
        return parent ? parent->find_type(name) : std::nullopt;
    else
        return it->second;
}
std::optional<Object> Scope::find_variable(const std::string& name) const {
    auto it = variables.find(name);
    if (it == variables.end())
        return parent ? parent->find_variable(name) : std::nullopt;
    else
        return it->second;
}
std::optional<Function> Scope::find_function(const std::string& name) const {
    auto it = functions.find(name);
    if (it == functions.end())
        return parent ? parent->find_function(name) : std::nullopt;
    else
        return it->second;
}
Object& Scope::get_variable(const std::string& name) const {
    auto it = variables.find(name);
    if (it == variables.end()) {
        if (!parent)
            throw_exception("cannot get varaible \"", name, '"');
        return parent->get_variable(name);
    } else
        return it->second;
}

BaseObject* InternalObject::clone() const {
    InternalObject* object = new InternalObject(*this);
    for (auto& func : object->functions) {
        for (auto& var : object->members)
            dynamic_cast<InternalFunction*>(func.second.base.get())->this_scope[var.first] =
                &var.second;
    }

    return object;
}

std::optional<Object> InternalFunction::run(const Scope& scope,
                                            const std::vector<Expression>& exprs) const {
    LLC_CHECK(parameters.size() == exprs.size());
    LLC_CHECK(definition != nullptr);

    for (int i = 0; i < (int)exprs.size(); i++)
        LLC_CHECK(definition->variables.find(parameters[i]) != definition->variables.end());

    std::map<std::string, Object> temp;

    for (int i = 0; i < (int)exprs.size(); i++)
        if (auto result = exprs[i](scope)) {
            temp[parameters[i]] = *result;
        } else
            throw_exception("void cannot be used as function parameter");

    for (const auto& var : temp)
        definition->variables[var.first] = var.second;

    for (const auto& var : this_scope)
        definition->variables[var.first] = *var.second;

    std::optional<Object> result;
    try {
        result = definition->run(scope);
    } catch (const std::optional<Object>& ret) {
        result = ret;
    }

    for (auto& var : this_scope)
        *var.second = definition->variables[var.first];

    return result;
}

std::optional<Object> ExternalFunction::run(const Scope& scope,
                                            const std::vector<Expression>& exprs) const {
    std::vector<Object> arguments;
    for (auto& expr : exprs) {
        if (auto result = expr(scope))
            arguments.push_back(*result);
        else
            throw_exception("void cannot be passes as argument to function");
    }
    return invoke(arguments);
}

Object MemberFunctionCall::evaluate(const Scope& scope) const {
    if (operand->original(scope).base->functions.find(function_name) ==
        operand->original(scope).base->functions.end())
        throw_exception("cannot find function \"", function_name, '"');

    if (auto result =
            operand->original(scope).base->functions[function_name].run(scope, arguments)) {
        return *result;
    } else {
        return {};
    }
}

Object TypeOp::evaluate(const Scope& scope) const {
    std::vector<Object> args;
    for (const auto& arg : arguments) {
        if (auto v = arg(scope))
            args.push_back(*v);
        else
            throw_exception("argument to constructor must-not be \"void\"");
    }
    if (args.size())
        return Object::construct(type, args);
    else
        return type;
}

void Expression::apply_parenthese() {
    int highest_prec = 0;
    for (const auto& operand : operands)
        highest_prec = std::max(highest_prec, operand->get_precedence());

    std::vector<int> parenthese_indices;
    int depth = 0;

    for (int i = 0; i < (int)operands.size(); i++) {
        if (dynamic_cast<LeftParenthese*>(operands[i].get()) ||
            dynamic_cast<LeftSquareBracket*>(operands[i].get())) {
            depth++;
            parenthese_indices.push_back(i);
        } else if (dynamic_cast<RightParenthese*>(operands[i].get()) ||
                   dynamic_cast<RightSquareBracket*>(operands[i].get())) {
            depth--;
            parenthese_indices.push_back(i);
        } else {
            operands[i]->set_precedence(operands[i]->get_precedence() + depth * highest_prec);
        }
    }

    for (int i = (int)parenthese_indices.size() - 1; i >= 0; i--)
        operands.erase(operands.begin() + parenthese_indices[i]);
}

void Expression::collapse() {
    apply_parenthese();

    int highest_prec = 0;
    for (const auto& operand : operands)
        highest_prec = std::max(highest_prec, operand->get_precedence());

    for (int prec = highest_prec; prec >= 0; prec--) {
        for (int i = 0; i < (int)operands.size(); i++) {
            if (operands[i]->get_precedence() == prec) {
                std::vector<int> merged = operands[i]->collapse(operands, i);
                std::sort(merged.begin(), merged.end(), std::greater<int>());
                for (int index : merged) {
                    LLC_CHECK(index >= 0);
                    LLC_CHECK(index < (int)operands.size());
                    operands.erase(operands.begin() + index);
                    if (index <= i)
                        i--;
                }
            }
        }
    }
}

std::optional<Object> IfElseChain::run(const Scope& scope) const {
    LLC_CHECK(conditions.size() == bodys.size() || conditions.size() == bodys.size() - 1);
    for (int i = 0; i < (int)bodys.size(); i++)
        LLC_CHECK(bodys[i] != nullptr);

    for (int i = 0; i < (int)conditions.size(); i++) {
        try {
            if (conditions[i](scope)->as<bool>())
                bodys[i]->run(scope);
        } catch (const std::optional<Object>& object) {
            throw object;
        }
    }

    if (conditions.size() == bodys.size() - 1) {
        bodys.back()->run(scope);
    }

    return std::nullopt;
}

std::optional<Object> For::run(const Scope& scope) const {
    LLC_CHECK(body != nullptr);

    for (initialization(*internal_scope); condition(*internal_scope)->as<bool>();
         updation(*internal_scope)->as<bool>()) {
        try {
            body->run(scope);
        } catch (const BreakLoop&) {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

std::optional<Object> While::run(const Scope& scope) const {
    LLC_CHECK(body != nullptr);

    while (condition(scope)->as<bool>()) {
        try {
            body->run(scope);
        } catch (const BreakLoop&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

}  // namespace llc