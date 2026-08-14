#include <iostream>
#include <string>
#include <vector>
#include <cctype>

// 1. Data Structures
enum class TokenType { LPAREN, RPAREN, NUMBER, STRING, SYMBOL };

struct Token {
    TokenType type;
    std::string value;
};

enum class ASTType { NUMBER, STRING, SYMBOL, LIST };

struct ASTNode {
    ASTType type;
    std::string value;             // Populated for NUMBER, STRING, SYMBOL
    std::vector<ASTNode> list;     // Populated for LIST
};

// 2. Lexer (Transforms raw string into a stream of tokens)
std::vector<Token> tokenize(const std::string& input) {
    
    std::vector<Token> tokens;
    size_t i = 0;

    while (i < input.size()) {
        if (std::isspace(input[i])) { i++; continue; }

        if (input[i] == ';') {
            while (i < input.size() && input[i] != '\n') {
                i++;
            }
            continue;
        }

        if (input[i] == '(') {
            tokens.push_back({TokenType::LPAREN, "("});
            i++;
        } 
        
        else if (input[i] == ')') {
            tokens.push_back({TokenType::RPAREN, ")"});
            i++;
        } 
        
        else if (input[i] == '"') { // String literal
            std::string s;
            i++; 
            while (i < input.size() && input[i] != '"'){
                if (input[i] == '\\' && i + 1 < input.size()) {
                    i++; // Skip backslash
                    switch (input[i]) {
                        case 'n':  s += '\n'; break; // Convert \n to real ASCII 10 newline
                        case 't':  s += '\t'; break; // Convert \t to tab
                        case '"':  s += '"';  break; // Convert \" to quote
                        case '\\': s += '\\'; break; // Convert \\ to backslash
                        default:   s += input[i]; break;
                    }
                } else {
                    s += input[i];
                }
                i++;
            }    
            i++; // skip closing "
            tokens.push_back({TokenType::STRING, s});
        } 
        
        else if (std::isdigit(input[i])) { // Integer literal
            std::string num;
            while (i < input.size() && std::isdigit(input[i])) num += input[i++];
            tokens.push_back({TokenType::NUMBER, num});
        } 
        
        else { // Symbol (+, print, x, var)
            std::string sym;
            while (i < input.size() && !std::isspace(input[i]) && 
                   input[i] != '(' && input[i] != ')') {
                sym += input[i++];
            }
            tokens.push_back({TokenType::SYMBOL, sym});
        }
    }
    
    return tokens;
}

// 3. Parser (Transforms tokens into an AST)
class Parser {
    std::vector<Token> tokens;
    size_t cursor = 0;

public:
    explicit Parser(std::vector<Token> tokens) : tokens(std::move(tokens)) {}

    ASTNode parse() {
        return parseExp();
    }

private:
    ASTNode parseExp() {
        const Token& token = tokens[cursor++];

        if (token.type == TokenType::LPAREN) {
            ASTNode node{ASTType::LIST, "", {}};
            // Parse elements recursively until closing parenthesis
            while (tokens[cursor].type != TokenType::RPAREN) {
                node.list.push_back(parseExp());
            }
            cursor++; // Consume RPAREN
            return node;
        } 
        
        if (token.type == TokenType::NUMBER) return {ASTType::NUMBER, token.value, {}};
        if (token.type == TokenType::STRING) return {ASTType::STRING, token.value, {}};
        return {ASTType::SYMBOL, token.value, {}}; // Default to SYMBOL
    }
};
