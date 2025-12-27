#pragma once

// TerraMath - Mathematical Expression Parser for Terrain Generation
// Supports custom equations for procedural terrain

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cmath>
#include <stdexcept>
#include <sstream>
#include <cctype>
#include <algorithm>

#include "FastNoiseLite.h"

namespace TerraMath {

// ============================================
// AST NODE TYPES
// ============================================
enum class NodeType {
    NUMBER,
    VARIABLE,
    BINARY_OP,
    UNARY_OP,
    FUNCTION_CALL
};

struct ASTNode {
    NodeType type;
    double value = 0;
    std::string name;
    char op = 0;
    std::vector<std::shared_ptr<ASTNode>> children;

    ASTNode(NodeType t) : type(t) {}
    ASTNode(double v) : type(NodeType::NUMBER), value(v) {}
    ASTNode(const std::string& varName) : type(NodeType::VARIABLE), name(varName) {}
};

using ASTNodePtr = std::shared_ptr<ASTNode>;

// ============================================
// TERRAIN NOISE FUNCTIONS
// ============================================
class TerrainFunctions {
public:
    FastNoiseLite simplex;
    FastNoiseLite cellular;
    FastNoiseLite perlin;
    int seed = 12345;

    TerrainFunctions() {
        setSeed(12345);
    }

    void setSeed(int s) {
        seed = s;
        simplex.SetSeed(seed);
        simplex.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        simplex.SetFrequency(0.01f);

        cellular.SetSeed(seed);
        cellular.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
        cellular.SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance);
        cellular.SetFrequency(0.02f);

        perlin.SetSeed(seed);
        perlin.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
        perlin.SetFrequency(0.01f);
    }

    // Basic simplex noise [-1, 1]
    double noise(double x, double z) {
        return simplex.GetNoise(static_cast<float>(x), static_cast<float>(z));
    }

    // Ridged noise [0, 1]
    double ridge(double x, double z) {
        double n = simplex.GetNoise(static_cast<float>(x), static_cast<float>(z));
        return 1.0 - std::abs(n);
    }

    // Fractal Brownian Motion
    double fbm(double x, double z, int octaves = 4) {
        double value = 0.0;
        double amplitude = 1.0;
        double frequency = 1.0;
        double maxValue = 0.0;

        for (int i = 0; i < octaves; i++) {
            value += amplitude * simplex.GetNoise(
                static_cast<float>(x * frequency),
                static_cast<float>(z * frequency)
            );
            maxValue += amplitude;
            amplitude *= 0.5;
            frequency *= 2.0;
        }

        return value / maxValue;
    }

    // Voronoi/Cellular noise [0, 1]
    double voronoi(double x, double z) {
        return (cellular.GetNoise(static_cast<float>(x), static_cast<float>(z)) + 1.0) * 0.5;
    }

    // Terraced value
    double terrace(double value, int steps) {
        if (steps <= 1) return value;
        return std::floor(value * steps) / (steps - 1);
    }
};

// ============================================
// TOKENIZER
// ============================================
enum class TokenType {
    NUMBER,
    IDENTIFIER,
    OPERATOR,
    LPAREN,
    RPAREN,
    COMMA,
    END
};

struct Token {
    TokenType type;
    std::string text;
    double value = 0;

    Token(TokenType t, const std::string& txt = "") : type(t), text(txt) {}
    Token(double v) : type(TokenType::NUMBER), value(v) {}
};

class Tokenizer {
public:
    std::string input;
    size_t pos = 0;

    Tokenizer(const std::string& expr) : input(expr), pos(0) {}

    Token nextToken() {
        skipWhitespace();

        if (pos >= input.length()) {
            return Token(TokenType::END);
        }

        char c = input[pos];

        // Numbers
        if (isdigit(c) || (c == '.' && pos + 1 < input.length() && isdigit(input[pos + 1]))) {
            return parseNumber();
        }

        // Identifiers (variables, functions)
        if (isalpha(c) || c == '_') {
            return parseIdentifier();
        }

        // Operators
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^') {
            pos++;
            return Token(TokenType::OPERATOR, std::string(1, c));
        }

        // Parentheses
        if (c == '(') {
            pos++;
            return Token(TokenType::LPAREN);
        }
        if (c == ')') {
            pos++;
            return Token(TokenType::RPAREN);
        }

        // Comma
        if (c == ',') {
            pos++;
            return Token(TokenType::COMMA);
        }

        // Unknown character, skip
        pos++;
        return nextToken();
    }

private:
    void skipWhitespace() {
        while (pos < input.length() && isspace(input[pos])) {
            pos++;
        }
    }

    Token parseNumber() {
        size_t start = pos;
        while (pos < input.length() && (isdigit(input[pos]) || input[pos] == '.')) {
            pos++;
        }
        std::string numStr = input.substr(start, pos - start);
        return Token(std::stod(numStr));
    }

    Token parseIdentifier() {
        size_t start = pos;
        while (pos < input.length() && (isalnum(input[pos]) || input[pos] == '_')) {
            pos++;
        }
        return Token(TokenType::IDENTIFIER, input.substr(start, pos - start));
    }
};

// ============================================
// EXPRESSION PARSER
// ============================================
class ExpressionParser {
public:
    Tokenizer* tokenizer = nullptr;
    Token currentToken = Token(TokenType::END);

    ASTNodePtr parse(const std::string& expr) {
        Tokenizer tok(expr);
        tokenizer = &tok;
        currentToken = tokenizer->nextToken();
        return parseExpression();
    }

    std::string validate(const std::string& expr) {
        try {
            auto ast = parse(expr);
            if (!ast) return "Failed to parse expression";

            // Try evaluating with dummy values
            std::unordered_map<std::string, double> vars;
            vars["x"] = 0;
            vars["z"] = 0;
            vars["seed"] = 12345;
            vars["baseHeight"] = 64;
            vars["seaLevel"] = 62;
            vars["continent"] = 0;
            vars["mountain"] = 0;
            vars["detail"] = 0;

            TerrainFunctions funcs;
            evaluate(ast, vars, funcs);
            return "";  // Valid
        } catch (const std::exception& e) {
            return e.what();
        }
    }

    double evaluate(ASTNodePtr node,
                    const std::unordered_map<std::string, double>& vars,
                    TerrainFunctions& funcs) {
        if (!node) return 0;

        switch (node->type) {
            case NodeType::NUMBER:
                return node->value;

            case NodeType::VARIABLE: {
                auto it = vars.find(node->name);
                if (it != vars.end()) {
                    return it->second;
                }
                throw std::runtime_error("Unknown variable: " + node->name);
            }

            case NodeType::BINARY_OP: {
                double left = evaluate(node->children[0], vars, funcs);
                double right = evaluate(node->children[1], vars, funcs);
                switch (node->op) {
                    case '+': return left + right;
                    case '-': return left - right;
                    case '*': return left * right;
                    case '/': return right != 0 ? left / right : 0;
                    case '^': return std::pow(left, right);
                    default: return 0;
                }
            }

            case NodeType::UNARY_OP: {
                double val = evaluate(node->children[0], vars, funcs);
                if (node->op == '-') return -val;
                return val;
            }

            case NodeType::FUNCTION_CALL: {
                return evaluateFunction(node, vars, funcs);
            }
        }

        return 0;
    }

private:
    void advance() {
        currentToken = tokenizer->nextToken();
    }

    ASTNodePtr parseExpression() {
        return parseAddSub();
    }

    ASTNodePtr parseAddSub() {
        auto left = parseMulDiv();

        while (currentToken.type == TokenType::OPERATOR &&
               (currentToken.text == "+" || currentToken.text == "-")) {
            char op = currentToken.text[0];
            advance();
            auto right = parseMulDiv();

            auto node = std::make_shared<ASTNode>(NodeType::BINARY_OP);
            node->op = op;
            node->children.push_back(left);
            node->children.push_back(right);
            left = node;
        }

        return left;
    }

    ASTNodePtr parseMulDiv() {
        auto left = parsePower();

        while (currentToken.type == TokenType::OPERATOR &&
               (currentToken.text == "*" || currentToken.text == "/")) {
            char op = currentToken.text[0];
            advance();
            auto right = parsePower();

            auto node = std::make_shared<ASTNode>(NodeType::BINARY_OP);
            node->op = op;
            node->children.push_back(left);
            node->children.push_back(right);
            left = node;
        }

        return left;
    }

    ASTNodePtr parsePower() {
        auto left = parseUnary();

        while (currentToken.type == TokenType::OPERATOR && currentToken.text == "^") {
            advance();
            auto right = parseUnary();

            auto node = std::make_shared<ASTNode>(NodeType::BINARY_OP);
            node->op = '^';
            node->children.push_back(left);
            node->children.push_back(right);
            left = node;
        }

        return left;
    }

    ASTNodePtr parseUnary() {
        if (currentToken.type == TokenType::OPERATOR && currentToken.text == "-") {
            advance();
            auto node = std::make_shared<ASTNode>(NodeType::UNARY_OP);
            node->op = '-';
            node->children.push_back(parsePrimary());
            return node;
        }

        if (currentToken.type == TokenType::OPERATOR && currentToken.text == "+") {
            advance();
        }

        return parsePrimary();
    }

    ASTNodePtr parsePrimary() {
        // Number
        if (currentToken.type == TokenType::NUMBER) {
            auto node = std::make_shared<ASTNode>(currentToken.value);
            advance();
            return node;
        }

        // Identifier (variable or function)
        if (currentToken.type == TokenType::IDENTIFIER) {
            std::string name = currentToken.text;
            advance();

            // Check if it's a function call
            if (currentToken.type == TokenType::LPAREN) {
                advance();  // consume '('

                auto node = std::make_shared<ASTNode>(NodeType::FUNCTION_CALL);
                node->name = name;

                // Parse arguments
                if (currentToken.type != TokenType::RPAREN) {
                    node->children.push_back(parseExpression());

                    while (currentToken.type == TokenType::COMMA) {
                        advance();
                        node->children.push_back(parseExpression());
                    }
                }

                if (currentToken.type == TokenType::RPAREN) {
                    advance();  // consume ')'
                }

                return node;
            }

            // It's a variable
            return std::make_shared<ASTNode>(name);
        }

        // Parenthesized expression
        if (currentToken.type == TokenType::LPAREN) {
            advance();  // consume '('
            auto expr = parseExpression();
            if (currentToken.type == TokenType::RPAREN) {
                advance();  // consume ')'
            }
            return expr;
        }

        // Default to 0
        return std::make_shared<ASTNode>(0.0);
    }

    double evaluateFunction(ASTNodePtr node,
                            const std::unordered_map<std::string, double>& vars,
                            TerrainFunctions& funcs) {
        const std::string& name = node->name;
        std::vector<double> args;
        for (auto& child : node->children) {
            args.push_back(evaluate(child, vars, funcs));
        }

        // Built-in math functions
        if (name == "sin" && args.size() >= 1) return std::sin(args[0]);
        if (name == "cos" && args.size() >= 1) return std::cos(args[0]);
        if (name == "tan" && args.size() >= 1) return std::tan(args[0]);
        if (name == "abs" && args.size() >= 1) return std::abs(args[0]);
        if (name == "sqrt" && args.size() >= 1) return std::sqrt(std::max(0.0, args[0]));
        if (name == "floor" && args.size() >= 1) return std::floor(args[0]);
        if (name == "ceil" && args.size() >= 1) return std::ceil(args[0]);
        if (name == "round" && args.size() >= 1) return std::round(args[0]);
        if (name == "exp" && args.size() >= 1) return std::exp(args[0]);
        if (name == "log" && args.size() >= 1) return args[0] > 0 ? std::log(args[0]) : 0;

        // Two-argument functions
        if (name == "pow" && args.size() >= 2) return std::pow(args[0], args[1]);
        if (name == "min" && args.size() >= 2) return std::min(args[0], args[1]);
        if (name == "max" && args.size() >= 2) return std::max(args[0], args[1]);
        if (name == "mod" && args.size() >= 2) return args[1] != 0 ? std::fmod(args[0], args[1]) : 0;

        // Three-argument functions
        if (name == "clamp" && args.size() >= 3) {
            return std::clamp(args[0], args[1], args[2]);
        }
        if (name == "lerp" && args.size() >= 3) {
            return args[0] + (args[1] - args[0]) * args[2];
        }
        if (name == "smoothstep" && args.size() >= 3) {
            double edge0 = args[0], edge1 = args[1], x = args[2];
            double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
            return t * t * (3 - 2 * t);
        }

        // Terrain noise functions
        if (name == "noise" && args.size() >= 2) {
            return funcs.noise(args[0], args[1]);
        }
        if (name == "ridge" && args.size() >= 2) {
            return funcs.ridge(args[0], args[1]);
        }
        if (name == "fbm" && args.size() >= 2) {
            int octaves = args.size() >= 3 ? static_cast<int>(args[2]) : 4;
            return funcs.fbm(args[0], args[1], octaves);
        }
        if (name == "voronoi" && args.size() >= 2) {
            return funcs.voronoi(args[0], args[1]);
        }
        if (name == "terrace" && args.size() >= 2) {
            return funcs.terrace(args[0], static_cast<int>(args[1]));
        }

        throw std::runtime_error("Unknown function: " + name);
    }
};

// ============================================
// TERRAIN EQUATION EVALUATOR
// ============================================
class TerrainEquation {
public:
    std::string equation;
    ASTNodePtr ast;
    ExpressionParser parser;
    TerrainFunctions functions;
    bool valid = false;
    std::string errorMessage;

    TerrainEquation() = default;

    bool compile(const std::string& expr) {
        equation = expr;
        errorMessage = parser.validate(expr);
        if (errorMessage.empty()) {
            ast = parser.parse(expr);
            valid = true;
        } else {
            valid = false;
            ast = nullptr;
        }
        return valid;
    }

    void setSeed(int seed) {
        functions.setSeed(seed);
    }

    double evaluate(double x, double z,
                    double baseHeight, double seaLevel,
                    double continent, double mountain, double detail) {
        if (!valid || !ast) return baseHeight;

        std::unordered_map<std::string, double> vars;
        vars["x"] = x;
        vars["z"] = z;
        vars["seed"] = static_cast<double>(functions.seed);
        vars["baseHeight"] = baseHeight;
        vars["seaLevel"] = seaLevel;
        vars["continent"] = continent;
        vars["mountain"] = mountain;
        vars["detail"] = detail;

        try {
            return parser.evaluate(ast, vars, functions);
        } catch (...) {
            return baseHeight;
        }
    }
};

} // namespace TerraMath
