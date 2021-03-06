/*
 * Cppcheck - A tool for static C/C++ code analysis
 * Copyright (C) 2007-2018 Cppcheck team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


//---------------------------------------------------------------------------
#include "checkstring.h"

#include "astutils.h"
#include "errorlogger.h"
#include "mathlib.h"
#include "settings.h"
#include "symboldatabase.h"
#include "token.h"
#include "tokenize.h"
#include "utils.h"

#include <cstddef>
#include <list>
#include <vector>
#include <stack>
#include <utility>

//---------------------------------------------------------------------------

// Register this check class (by creating a static instance of it)
namespace {
    CheckString instance;
}

// CWE ids used:
static const struct CWE CWE570(570U);   // Expression is Always False
static const struct CWE CWE571(571U);   // Expression is Always True
static const struct CWE CWE595(595U);   // Comparison of Object References Instead of Object Contents
static const struct CWE CWE628(628U);   // Function Call with Incorrectly Specified Arguments
static const struct CWE CWE665(665U);   // Improper Initialization
static const struct CWE CWE758(758U);   // Reliance on Undefined, Unspecified, or Implementation-Defined Behavior

//---------------------------------------------------------------------------
// Writing string literal is UB
//---------------------------------------------------------------------------
void CheckString::stringLiteralWrite()
{
    const SymbolDatabase *symbolDatabase = _tokenizer->getSymbolDatabase();
    const std::size_t functions = symbolDatabase->functionScopes.size();
    for (std::size_t i = 0; i < functions; ++i) {
        const Scope * scope = symbolDatabase->functionScopes[i];
        for (const Token* tok = scope->classStart->next(); tok != scope->classEnd; tok = tok->next()) {
            if (!tok->variable() || !tok->variable()->isPointer())
                continue;
            const Token *str = tok->getValueTokenMinStrSize();
            if (!str)
                continue;
            if (Token::Match(tok, "%var% [") && Token::simpleMatch(tok->linkAt(1), "] ="))
                stringLiteralWriteError(tok, str);
            else if (Token::Match(tok->previous(), "* %var% ="))
                stringLiteralWriteError(tok, str);
        }
    }
}

void CheckString::stringLiteralWriteError(const Token *tok, const Token *strValue)
{
    std::list<const Token *> callstack;
    callstack.push_back(tok);
    if (strValue)
        callstack.push_back(strValue);

    std::string errmsg("Modifying string literal");
    if (strValue) {
        std::string s = strValue->strValue();
        if (s.size() > 15U)
            s = s.substr(0,13) + "..";
        errmsg += " \"" + s + "\"";
    }
    errmsg += " directly or indirectly is undefined behaviour.";

    reportError(callstack, Severity::error, "stringLiteralWrite", errmsg, CWE758, false);
}

//---------------------------------------------------------------------------
// Check for string comparison involving two static strings.
// if(strcmp("00FF00","00FF00")==0) // <- statement is always true
//---------------------------------------------------------------------------
void CheckString::checkAlwaysTrueOrFalseStringCompare()
{
    if (!_settings->isEnabled(Settings::WARNING))
        return;

    for (const Token* tok = _tokenizer->tokens(); tok; tok = tok->next()) {
        if (tok->isName() && tok->strAt(1) == "(" && Token::Match(tok, "memcmp|strncmp|strcmp|stricmp|strverscmp|bcmp|strcmpi|strcasecmp|strncasecmp|strncasecmp_l|strcasecmp_l|wcsncasecmp|wcscasecmp|wmemcmp|wcscmp|wcscasecmp_l|wcsncasecmp_l|wcsncmp|_mbscmp|_memicmp|_memicmp_l|_stricmp|_wcsicmp|_mbsicmp|_stricmp_l|_wcsicmp_l|_mbsicmp_l")) {
            if (Token::Match(tok->tokAt(2), "%str% , %str% ,|)")) {
                const std::string &str1 = tok->strAt(2);
                const std::string &str2 = tok->strAt(4);
                if (!tok->isExpandedMacro() && !tok->tokAt(2)->isExpandedMacro() && !tok->tokAt(4)->isExpandedMacro())
                    alwaysTrueFalseStringCompareError(tok, str1, str2);
                tok = tok->tokAt(5);
            } else if (Token::Match(tok->tokAt(2), "%name% , %name% ,|)")) {
                const std::string &str1 = tok->strAt(2);
                const std::string &str2 = tok->strAt(4);
                if (str1 == str2)
                    alwaysTrueStringVariableCompareError(tok, str1, str2);
                tok = tok->tokAt(5);
            } else if (Token::Match(tok->tokAt(2), "%name% . c_str ( ) , %name% . c_str ( ) ,|)")) {
                const std::string &str1 = tok->strAt(2);
                const std::string &str2 = tok->strAt(8);
                if (str1 == str2)
                    alwaysTrueStringVariableCompareError(tok, str1, str2);
                tok = tok->tokAt(13);
            }
        } else if (tok->isName() && Token::Match(tok, "QString :: compare ( %str% , %str% )")) {
            const std::string &str1 = tok->strAt(4);
            const std::string &str2 = tok->strAt(6);
            alwaysTrueFalseStringCompareError(tok, str1, str2);
            tok = tok->tokAt(7);
        } else if (Token::Match(tok, "!!+ %str% ==|!= %str% !!+")) {
            const std::string &str1 = tok->strAt(1);
            const std::string &str2 = tok->strAt(3);
            alwaysTrueFalseStringCompareError(tok, str1, str2);
            tok = tok->tokAt(5);
        }
        if (!tok)
            break;
    }
}

void CheckString::alwaysTrueFalseStringCompareError(const Token *tok, const std::string& str1, const std::string& str2)
{
    const std::size_t stringLen = 10;
    const std::string string1 = (str1.size() < stringLen) ? str1 : (str1.substr(0, stringLen-2) + "..");
    const std::string string2 = (str2.size() < stringLen) ? str2 : (str2.substr(0, stringLen-2) + "..");

    reportError(tok, Severity::warning, "staticStringCompare",
                "Unnecessary comparison of static strings.\n"
                "The compared strings, '" + string1 + "' and '" + string2 + "', are always " + (str1==str2?"identical":"unequal") + ". "
                "Therefore the comparison is unnecessary and looks suspicious.", (str1==str2)?CWE571:CWE570, false);
}

void CheckString::alwaysTrueStringVariableCompareError(const Token *tok, const std::string& str1, const std::string& str2)
{
    reportError(tok, Severity::warning, "stringCompare",
                "Comparison of identical string variables.\n"
                "The compared strings, '" + str1 + "' and '" + str2 + "', are identical. "
                "This could be a logic bug.", CWE571, false);
}


//-----------------------------------------------------------------------------
// Detect "str == '\0'" where "*str == '\0'" is correct.
// Comparing char* with each other instead of using strcmp()
//-----------------------------------------------------------------------------
void CheckString::checkSuspiciousStringCompare()
{
    if (!_settings->isEnabled(Settings::WARNING))
        return;

    const SymbolDatabase* symbolDatabase = _tokenizer->getSymbolDatabase();
    const std::size_t functions = symbolDatabase->functionScopes.size();
    for (std::size_t i = 0; i < functions; ++i) {
        const Scope * scope = symbolDatabase->functionScopes[i];
        for (const Token* tok = scope->classStart->next(); tok != scope->classEnd; tok = tok->next()) {
            if (tok->tokType() != Token::eComparisonOp)
                continue;

            const Token* varTok = tok->astOperand1();
            const Token* litTok = tok->astOperand2();
            if (!varTok || !litTok)  // <- failed to create AST for comparison
                continue;
            if (Token::Match(varTok, "%char%|%num%|%str%"))
                std::swap(varTok, litTok);
            else if (!Token::Match(litTok, "%char%|%num%|%str%"))
                continue;

            // Pointer addition?
            if (varTok->str() == "+" && _tokenizer->isC()) {
                const Token *tokens[2] = { varTok->astOperand1(), varTok->astOperand2() };
                for (int nr = 0; nr < 2; nr++) {
                    const Token *t = tokens[nr];
                    while (t && (t->str() == "." || t->str() == "::"))
                        t = t->astOperand2();
                    if (t && t->variable() && t->variable()->isPointer())
                        varTok = t;
                }
            }

            if (varTok->str() == "*") {
                if (!_tokenizer->isC() || varTok->astOperand2() != nullptr || litTok->tokType() != Token::eString)
                    continue;
                varTok = varTok->astOperand1();
            }

            while (varTok && (varTok->str() == "." || varTok->str() == "::"))
                varTok = varTok->astOperand2();
            if (!varTok || !varTok->isName())
                continue;

            const Variable *var = varTok->variable();

            while (Token::Match(varTok->astParent(), "[.*]"))
                varTok = varTok->astParent();
            const std::string varname = varTok->expressionString();

            const bool ischar(litTok->tokType() == Token::eChar);
            if (litTok->tokType() == Token::eString) {
                if (_tokenizer->isC() || (var && var->isArrayOrPointer()))
                    suspiciousStringCompareError(tok, varname);
            } else if (ischar && var && var->isPointer()) {
                suspiciousStringCompareError_char(tok, varname);
            }
        }
    }
}

void CheckString::suspiciousStringCompareError(const Token* tok, const std::string& var)
{
    reportError(tok, Severity::warning, "literalWithCharPtrCompare",
                "String literal compared with variable '" + var + "'. Did you intend to use strcmp() instead?", CWE595, false);
}

void CheckString::suspiciousStringCompareError_char(const Token* tok, const std::string& var)
{
    reportError(tok, Severity::warning, "charLiteralWithCharPtrCompare",
                "Char literal compared with pointer '" + var + "'. Did you intend to dereference it?", CWE595, false);
}


//---------------------------------------------------------------------------
// Adding C-string and char with operator+
//---------------------------------------------------------------------------

static bool isChar(const Variable* var)
{
    return (var && !var->isPointer() && !var->isArray() && var->typeStartToken()->str() == "char");
}

void CheckString::strPlusChar()
{
    const SymbolDatabase* symbolDatabase = _tokenizer->getSymbolDatabase();
    const std::size_t functions = symbolDatabase->functionScopes.size();
    for (std::size_t i = 0; i < functions; ++i) {
        const Scope * scope = symbolDatabase->functionScopes[i];
        for (const Token* tok = scope->classStart->next(); tok != scope->classEnd; tok = tok->next()) {
            if (tok->str() == "+") {
                if (tok->astOperand1() && (tok->astOperand1()->tokType() == Token::eString)) { // string literal...
                    if (tok->astOperand2() && (tok->astOperand2()->tokType() == Token::eChar || isChar(tok->astOperand2()->variable()))) // added to char variable or char constant
                        strPlusCharError(tok);
                }
            }
        }
    }
}

void CheckString::strPlusCharError(const Token *tok)
{
    reportError(tok, Severity::error, "strPlusChar", "Unusual pointer arithmetic. A value of type 'char' is added to a string literal.", CWE665, false);
}

//---------------------------------------------------------------------------
// Implicit casts of string literals to bool
// Comparing string literal with strlen() with wrong length
//---------------------------------------------------------------------------
void CheckString::checkIncorrectStringCompare()
{
    if (!_settings->isEnabled(Settings::WARNING))
        return;

    const SymbolDatabase *symbolDatabase = _tokenizer->getSymbolDatabase();
    const std::size_t functions = symbolDatabase->functionScopes.size();
    for (std::size_t i = 0; i < functions; ++i) {
        const Scope * scope = symbolDatabase->functionScopes[i];
        for (const Token* tok = scope->classStart->next(); tok != scope->classEnd; tok = tok->next()) {
            // skip "assert(str && ..)" and "assert(.. && str)"
            if ((endsWith(tok->str(), "assert", 6) || endsWith(tok->str(), "ASSERT", 6)) &&
                Token::Match(tok, "%name% (") &&
                (Token::Match(tok->tokAt(2), "%str% &&") || Token::Match(tok->next()->link()->tokAt(-2), "&& %str% )")))
                tok = tok->next()->link();

            if (Token::simpleMatch(tok, ". substr (") && Token::Match(tok->tokAt(3)->nextArgument(), "%num% )")) {
                MathLib::biguint clen = MathLib::toULongNumber(tok->linkAt(2)->strAt(-1));
                const Token* begin = tok->previous();
                for (;;) { // Find start of statement
                    while (begin->link() && Token::Match(begin, "]|)|>"))
                        begin = begin->link()->previous();
                    if (Token::Match(begin->previous(), ".|::"))
                        begin = begin->tokAt(-2);
                    else
                        break;
                }
                begin = begin->previous();
                const Token* end = tok->linkAt(2)->next();
                if (Token::Match(begin->previous(), "%str% ==|!=") && begin->strAt(-2) != "+") {
                    std::size_t slen = Token::getStrLength(begin->previous());
                    if (clen != slen) {
                        incorrectStringCompareError(tok->next(), "substr", begin->strAt(-1));
                    }
                } else if (Token::Match(end, "==|!= %str% !!+")) {
                    std::size_t slen = Token::getStrLength(end->next());
                    if (clen != slen) {
                        incorrectStringCompareError(tok->next(), "substr", end->strAt(1));
                    }
                }
            } else if (Token::Match(tok, "&&|%oror%|( %str% &&|%oror%|)") && !Token::Match(tok, "( %str% )")) {
                incorrectStringBooleanError(tok->next(), tok->strAt(1));
            } else if (Token::Match(tok, "if|while ( %str% )")) {
                incorrectStringBooleanError(tok->tokAt(2), tok->strAt(2));
            }
        }
    }
}

void CheckString::incorrectStringCompareError(const Token *tok, const std::string& func, const std::string &string)
{
    reportError(tok, Severity::warning, "incorrectStringCompare", "String literal " + string + " doesn't match length argument for " + func + "().", CWE570, false);
}

void CheckString::incorrectStringBooleanError(const Token *tok, const std::string& string)
{
    reportError(tok, Severity::warning, "incorrectStringBooleanError", "Conversion of string literal " + string + " to bool always evaluates to true.", CWE571, false);
}

//---------------------------------------------------------------------------
// always true: strcmp(str,"a")==0 || strcmp(str,"b")
// TODO: Library configuration for string comparison functions
//---------------------------------------------------------------------------
void CheckString::overlappingStrcmp()
{
    if (!_settings->isEnabled(Settings::WARNING))
        return;

    const SymbolDatabase *symbolDatabase = _tokenizer->getSymbolDatabase();
    const std::size_t functions = symbolDatabase->functionScopes.size();
    for (std::size_t i = 0; i < functions; ++i) {
        const Scope * scope = symbolDatabase->functionScopes[i];
        for (const Token* tok = scope->classStart->next(); tok != scope->classEnd; tok = tok->next()) {
            if (tok->str() != "||")
                continue;
            std::list<const Token *> equals0;
            std::list<const Token *> notEquals0;
            std::stack<const Token *> tokens;
            tokens.push(tok);
            while (!tokens.empty()) {
                const Token * const t = tokens.top();
                tokens.pop();
                if (!t)
                    continue;
                if (t->str() == "||") {
                    tokens.push(t->astOperand1());
                    tokens.push(t->astOperand2());
                    continue;
                }
                if (t->str() == "==") {
                    if (Token::simpleMatch(t->astOperand1(), "(") && Token::simpleMatch(t->astOperand2(), "0"))
                        equals0.push_back(t->astOperand1());
                    else if (Token::simpleMatch(t->astOperand2(), "(") && Token::simpleMatch(t->astOperand1(), "0"))
                        equals0.push_back(t->astOperand2());
                    continue;
                }
                if (t->str() == "!=") {
                    if (Token::simpleMatch(t->astOperand1(), "(") && Token::simpleMatch(t->astOperand2(), "0"))
                        notEquals0.push_back(t->astOperand1());
                    else if (Token::simpleMatch(t->astOperand2(), "(") && Token::simpleMatch(t->astOperand1(), "0"))
                        notEquals0.push_back(t->astOperand2());
                    continue;
                }
                if (t->str() == "!" && Token::simpleMatch(t->astOperand1(), "("))
                    equals0.push_back(t->astOperand1());
                else if (t->str() == "(")
                    notEquals0.push_back(t);
            }

            for (std::list<const Token *>::const_iterator eq0 = equals0.begin(); eq0 != equals0.end(); ++eq0) {
                for (std::list<const Token *>::const_iterator ne0 = notEquals0.begin(); ne0 != notEquals0.end(); ++ne0) {
                    const Token *tok1 = *eq0;
                    const Token *tok2 = *ne0;
                    if (!Token::simpleMatch(tok1->previous(), "strcmp ("))
                        continue;
                    if (!Token::simpleMatch(tok2->previous(), "strcmp ("))
                        continue;
                    const std::vector<const Token *> args1 = getArguments(tok1->previous());
                    const std::vector<const Token *> args2 = getArguments(tok2->previous());
                    if (args1.size() != 2 || args2.size() != 2)
                        continue;
                    if (args1[1]->isLiteral() &&
                        args2[1]->isLiteral() &&
                        args1[1]->str() != args2[1]->str() &&
                        isSameExpression(_tokenizer->isCPP(), true, args1[0], args2[0], _settings->library, true))
                        overlappingStrcmpError(tok1, tok2);
                }
            }
        }
    }
}

void CheckString::overlappingStrcmpError(const Token *eq0, const Token *ne0)
{
    std::string eq0Expr(eq0 ? eq0->expressionString() : std::string("strcmp(x,\"abc\")"));
    if (eq0 && eq0->astParent()->str() == "!")
        eq0Expr = "!" + eq0Expr;
    else
        eq0Expr += " == 0";

    const std::string ne0Expr = (ne0 ? ne0->expressionString() : std::string("strcmp(x,\"def\")")) + " != 0";

    reportError(ne0, Severity::warning, "overlappingStrcmp", "The expression '" + ne0Expr + "' is suspicious. It overlaps '" + eq0Expr + "'.");
}

//---------------------------------------------------------------------------
// Overlapping source and destination passed to sprintf().
// TODO: Library configuration for overlapping arguments
//---------------------------------------------------------------------------
void CheckString::sprintfOverlappingData()
{
    const SymbolDatabase* symbolDatabase = _tokenizer->getSymbolDatabase();
    const std::size_t functions = symbolDatabase->functionScopes.size();
    for (std::size_t i = 0; i < functions; ++i) {
        const Scope * scope = symbolDatabase->functionScopes[i];
        for (const Token* tok = scope->classStart->next(); tok != scope->classEnd; tok = tok->next()) {
            if (!Token::Match(tok, "sprintf|snprintf|swprintf ("))
                continue;

            const std::vector<const Token *> args = getArguments(tok);

            const int formatString = Token::simpleMatch(tok, "sprintf") ? 1 : 2;
            for (unsigned int argnr = formatString + 1; argnr < args.size(); ++argnr) {
                bool same = isSameExpression(_tokenizer->isCPP(),
                                             false,
                                             args[0],
                                             args[argnr],
                                             _settings->library,
                                             true);
                if (same) {
                    sprintfOverlappingDataError(args[argnr], args[argnr]->expressionString());
                }
            }
        }
    }
}

void CheckString::sprintfOverlappingDataError(const Token *tok, const std::string &varname)
{
    reportError(tok, Severity::error, "sprintfOverlappingData",
                "Undefined behavior: Variable '" + varname + "' is used as parameter and destination in s[n]printf().\n"
                "The variable '" + varname + "' is used both as a parameter and as destination in "
                "s[n]printf(). The origin and destination buffers overlap. Quote from glibc (C-library) "
                "documentation (http://www.gnu.org/software/libc/manual/html_mono/libc.html#Formatted-Output-Functions): "
                "\"If copying takes place between objects that overlap as a result of a call "
                "to sprintf() or snprintf(), the results are undefined.\"", CWE628, false);
}
