#ifndef C2S_C_CAST_H
#define C2S_C_CAST_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace c2s {

class CIntLit;
class CFloatLit;
class CCharLit;
class CStringLit;
class CIdent;
class CUnary;
class CBinary;
class CAssign;
class CTernary;
class CCall;
class CIndex;
class CMember;
class CCast;
class CSizeof;
class CComma;
class CExprStmt;
class CEmpty;
class CCompound;
class CIf;
class CWhile;
class CDoWhile;
class CFor;
class CSwitch;
class CCase;
class CBreak;
class CContinue;
class CReturn;
class CGoto;
class CLabel;
class CDeclStmt;
class CBeyond;
class CDeclaration;
class CFunctionDef;

class CVisitor {
public:
    virtual ~CVisitor() = default;

    virtual void visit(CIntLit &) = 0;
    virtual void visit(CFloatLit &) = 0;
    virtual void visit(CCharLit &) = 0;
    virtual void visit(CStringLit &) = 0;
    virtual void visit(CIdent &) = 0;
    virtual void visit(CUnary &) = 0;
    virtual void visit(CBinary &) = 0;
    virtual void visit(CAssign &) = 0;
    virtual void visit(CTernary &) = 0;
    virtual void visit(CCall &) = 0;
    virtual void visit(CIndex &) = 0;
    virtual void visit(CMember &) = 0;
    virtual void visit(CCast &) = 0;
    virtual void visit(CSizeof &) = 0;
    virtual void visit(CComma &) = 0;
    virtual void visit(CExprStmt &) = 0;
    virtual void visit(CEmpty &) = 0;
    virtual void visit(CCompound &) = 0;
    virtual void visit(CIf &) = 0;
    virtual void visit(CWhile &) = 0;
    virtual void visit(CDoWhile &) = 0;
    virtual void visit(CFor &) = 0;
    virtual void visit(CSwitch &) = 0;
    virtual void visit(CCase &) = 0;
    virtual void visit(CBreak &) = 0;
    virtual void visit(CContinue &) = 0;
    virtual void visit(CReturn &) = 0;
    virtual void visit(CGoto &) = 0;
    virtual void visit(CLabel &) = 0;
    virtual void visit(CDeclStmt &) = 0;
    virtual void visit(CBeyond &) = 0;
};

class CNode {
public:
    virtual ~CNode() = default;
    virtual void accept(CVisitor &v) = 0;

    std::size_t offset() const { return offset_; }
    void setOffset(std::size_t offset) { offset_ = offset; }

protected:
    CNode() = default;

private:
    std::size_t offset_ = 0;
};

class CType {
public:
    enum class Kind {
        Void, Char, Int, Float, Double,
        Pointer, Array, Function,
        Struct, Union, Enum,
        Named
    };

    explicit CType(Kind kind) : kind_(kind) {}

    Kind kind() const { return kind_; }

    bool isUnsigned() const { return isUnsigned_; }
    bool isSignedExplicit() const { return isSignedExplicit_; }
    bool isShort() const { return isShort_; }
    bool isLong() const { return isLong_; }
    void setUnsigned() { isUnsigned_ = true; }
    void setSignedExplicit() { isSignedExplicit_ = true; }
    void setShort() { isShort_ = true; }
    void setLong() { isLong_ = true; }

    bool isConst() const { return isConst_; }
    bool isVolatile() const { return isVolatile_; }
    void setConst() { isConst_ = true; }
    void setVolatile() { isVolatile_ = true; }

    const CType *base() const { return base_.get(); }
    CType *base() { return base_.get(); }
    void setBase(std::unique_ptr<CType> base) { base_ = std::move(base); }
    std::unique_ptr<CType> takeBase() { return std::move(base_); }

    class CExprHolder;
    bool hasLength() const { return lengthText_ != nullptr; }

    void setLength(std::unique_ptr<CNode> length) { lengthText_ = std::move(length); }
    CNode *length() const { return lengthText_.get(); }

    struct Param {
        std::string name;
        std::unique_ptr<CType> type;
    };
    std::vector<Param> &params() { return params_; }
    const std::vector<Param> &params() const { return params_; }
    bool isVariadic() const { return isVariadic_; }
    void setVariadic() { isVariadic_ = true; }

    bool isProtoVoid() const { return isProtoVoid_; }
    void setProtoVoid() { isProtoVoid_ = true; }

    const std::string &tag() const { return tag_; }
    void setTag(std::string tag) { tag_ = std::move(tag); }

    struct Member {
        std::string name;
        std::unique_ptr<CType> type;
        std::unique_ptr<CNode> bitWidth;
    };
    std::vector<Member> &members() { return members_; }
    const std::vector<Member> &members() const { return members_; }
    bool hasMemberList() const { return hasMemberList_; }
    void setHasMemberList() { hasMemberList_ = true; }

    struct Enumerator {
        std::string name;
        std::unique_ptr<CNode> value;
    };
    std::vector<Enumerator> &enumerators() { return enumerators_; }
    const std::vector<Enumerator> &enumerators() const { return enumerators_; }

    std::unique_ptr<CType> clone() const;

    std::string describe() const;

private:
    Kind kind_;
    bool isUnsigned_ = false;
    bool isSignedExplicit_ = false;
    bool isShort_ = false;
    bool isLong_ = false;
    bool isConst_ = false;
    bool isVolatile_ = false;
    bool isVariadic_ = false;
    bool isProtoVoid_ = false;
    bool hasMemberList_ = false;
    std::unique_ptr<CType> base_;
    std::unique_ptr<CNode> lengthText_;
    std::vector<Param> params_;
    std::string tag_;
    std::vector<Member> members_;
    std::vector<Enumerator> enumerators_;
};

using CTypePtr = std::unique_ptr<CType>;

class CExpr : public CNode {
protected:
    CExpr() = default;
};

using CExprPtr = std::unique_ptr<CExpr>;

class CIntLit : public CExpr {
public:
    CIntLit(long long value, std::string spelling)
        : value_(value), spelling_(std::move(spelling)) {}

    long long value() const { return value_; }
    const std::string &spelling() const { return spelling_; }
    bool isUnsigned() const { return isUnsigned_; }
    bool isLong() const { return isLong_; }
    bool isHexOrOctal() const { return isHexOrOctal_; }
    void setSuffixes(bool isUnsigned, bool isLong, bool hexOrOctal) {
        isUnsigned_ = isUnsigned;
        isLong_ = isLong;
        isHexOrOctal_ = hexOrOctal;
    }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    long long value_;
    std::string spelling_;
    bool isUnsigned_ = false;
    bool isLong_ = false;
    bool isHexOrOctal_ = false;
};

class CFloatLit : public CExpr {
public:
    CFloatLit(double value, std::string spelling)
        : value_(value), spelling_(std::move(spelling)) {}

    double value() const { return value_; }
    const std::string &spelling() const { return spelling_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    double value_;
    std::string spelling_;
};

class CCharLit : public CExpr {
public:
    CCharLit(long long value, std::string spelling)
        : value_(value), spelling_(std::move(spelling)) {}

    long long value() const { return value_; }
    const std::string &spelling() const { return spelling_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    long long value_;
    std::string spelling_;
};

class CStringLit : public CExpr {
public:
    CStringLit(std::string text, std::string spelling)
        : text_(std::move(text)), spelling_(std::move(spelling)) {}

    const std::string &text() const { return text_; }
    const std::string &spelling() const { return spelling_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    std::string text_;
    std::string spelling_;
};

class CIdent : public CExpr {
public:
    explicit CIdent(std::string name) : name_(std::move(name)) {}

    const std::string &name() const { return name_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    std::string name_;
};

class CUnary : public CExpr {
public:
    CUnary(std::string op, bool prefix, CExprPtr operand)
        : op_(std::move(op)), prefix_(prefix), operand_(std::move(operand)) {}

    const std::string &op() const { return op_; }
    bool prefix() const { return prefix_; }
    CExpr &operand() { return *operand_; }
    CExprPtr &operandRef() { return operand_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    std::string op_;
    bool prefix_;
    CExprPtr operand_;
};

class CBinary : public CExpr {
public:
    CBinary(std::string op, CExprPtr lhs, CExprPtr rhs)
        : op_(std::move(op)), lhs_(std::move(lhs)), rhs_(std::move(rhs)) {}

    const std::string &op() const { return op_; }
    CExpr &lhs() { return *lhs_; }
    CExpr &rhs() { return *rhs_; }
    CExprPtr &left() { return lhs_; }
    CExprPtr &right() { return rhs_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    std::string op_;
    CExprPtr lhs_;
    CExprPtr rhs_;
};

class CAssign : public CExpr {
public:
    CAssign(std::string op, CExprPtr target, CExprPtr value)
        : op_(std::move(op)), target_(std::move(target)), value_(std::move(value)) {}

    const std::string &op() const { return op_; }
    CExpr &target() { return *target_; }
    CExpr &value() { return *value_; }
    CExprPtr &targetRef() { return target_; }
    CExprPtr &valueRef() { return value_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    std::string op_;
    CExprPtr target_;
    CExprPtr value_;
};

class CTernary : public CExpr {
public:
    CTernary(CExprPtr cond, CExprPtr thenArm, CExprPtr elseArm)
        : cond_(std::move(cond)), then_(std::move(thenArm)), else_(std::move(elseArm)) {}

    CExpr &cond() { return *cond_; }
    CExpr &thenArm() { return *then_; }
    CExpr &elseArm() { return *else_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    CExprPtr cond_;
    CExprPtr then_;
    CExprPtr else_;
};

class CCall : public CExpr {
public:
    explicit CCall(CExprPtr callee) : callee_(std::move(callee)) {}

    CExpr &callee() { return *callee_; }
    void add(CExprPtr argument) { args_.push_back(std::move(argument)); }
    std::vector<CExprPtr> &args() { return args_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    CExprPtr callee_;
    std::vector<CExprPtr> args_;
};

class CIndex : public CExpr {
public:
    CIndex(CExprPtr base, CExprPtr index)
        : base_(std::move(base)), index_(std::move(index)) {}

    CExpr &base() { return *base_; }
    CExpr &index() { return *index_; }
    CExprPtr &baseRef() { return base_; }
    CExprPtr &indexRef() { return index_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    CExprPtr base_;
    CExprPtr index_;
};

class CMember : public CExpr {
public:
    CMember(CExprPtr object, std::string name, bool arrow)
        : object_(std::move(object)), name_(std::move(name)), arrow_(arrow) {}

    CExpr &object() { return *object_; }
    const std::string &name() const { return name_; }
    bool arrow() const { return arrow_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    CExprPtr object_;
    std::string name_;
    bool arrow_;
};

class CCast : public CExpr {
public:
    CCast(CTypePtr type, CExprPtr operand)
        : type_(std::move(type)), operand_(std::move(operand)) {}

    const CType &type() const { return *type_; }
    CExpr &operand() { return *operand_; }
    CExprPtr &operandRef() { return operand_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    CTypePtr type_;
    CExprPtr operand_;
};

class CSizeof : public CExpr {
public:
    explicit CSizeof(CExprPtr operand) : operand_(std::move(operand)) {}
    explicit CSizeof(CTypePtr type) : type_(std::move(type)) {}

    bool ofType() const { return type_ != nullptr; }
    CExpr *operand() { return operand_.get(); }
    const CType *type() const { return type_.get(); }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    CExprPtr operand_;
    CTypePtr type_;
};

class CComma : public CExpr {
public:
    CComma(CExprPtr left, CExprPtr right)
        : left_(std::move(left)), right_(std::move(right)) {}

    CExpr &left() { return *left_; }
    CExpr &right() { return *right_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    CExprPtr left_;
    CExprPtr right_;
};

class CStmt : public CNode {
protected:
    CStmt() = default;
};

using CStmtPtr = std::unique_ptr<CStmt>;

class CInit {
public:
    CInit() = default;
    explicit CInit(CExprPtr expr) : expr_(std::move(expr)) {}

    bool isList() const { return expr_ == nullptr; }
    CExpr *expr() { return expr_.get(); }
    CExprPtr &exprRef() { return expr_; }
    std::vector<CInit> &items() { return items_; }
    const std::vector<CInit> &items() const { return items_; }
    void add(CInit item) { items_.push_back(std::move(item)); }

private:
    CExprPtr expr_;
    std::vector<CInit> items_;
};

class CDeclaration : public CNode {
public:
    enum class Storage { None, Typedef, Extern, Static, Auto, Register };

    struct Declarator {
        std::string name;
        CTypePtr type;
        std::unique_ptr<CInit> init;
        std::size_t offset = 0;
    };

    Storage storage() const { return storage_; }
    void setStorage(Storage storage) { storage_ = storage; }

    std::vector<Declarator> &declarators() { return declarators_; }
    const std::vector<Declarator> &declarators() const { return declarators_; }
    void add(Declarator declarator) { declarators_.push_back(std::move(declarator)); }

    const CType *bareType() const { return bareType_.get(); }
    void setBareType(CTypePtr type) { bareType_ = std::move(type); }

    void accept(CVisitor &) override {}

private:
    Storage storage_ = Storage::None;
    std::vector<Declarator> declarators_;
    CTypePtr bareType_;
};

class CDeclStmt : public CStmt {
public:
    explicit CDeclStmt(std::unique_ptr<CDeclaration> decl) : decl_(std::move(decl)) {}

    CDeclaration &decl() { return *decl_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    std::unique_ptr<CDeclaration> decl_;
};

class CExprStmt : public CStmt {
public:
    explicit CExprStmt(CExprPtr expr) : expr_(std::move(expr)) {}

    CExpr &expr() { return *expr_; }
    CExprPtr &exprRef() { return expr_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    CExprPtr expr_;
};

class CEmpty : public CStmt {
public:
    void accept(CVisitor &v) override { v.visit(*this); }
};

class CCompound : public CStmt {
public:
    void add(CStmtPtr stmt) { body_.push_back(std::move(stmt)); }
    std::vector<CStmtPtr> &body() { return body_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    std::vector<CStmtPtr> body_;
};

class CIf : public CStmt {
public:
    CIf(CExprPtr cond, CStmtPtr thenArm, CStmtPtr elseArm)
        : cond_(std::move(cond)), then_(std::move(thenArm)), else_(std::move(elseArm)) {}

    CExpr &cond() { return *cond_; }
    CStmt &thenArm() { return *then_; }
    CStmt *elseArm() { return else_.get(); }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    CExprPtr cond_;
    CStmtPtr then_;
    CStmtPtr else_;
};

class CWhile : public CStmt {
public:
    CWhile(CExprPtr cond, CStmtPtr body)
        : cond_(std::move(cond)), body_(std::move(body)) {}

    CExpr &cond() { return *cond_; }
    CStmt &body() { return *body_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    CExprPtr cond_;
    CStmtPtr body_;
};

class CDoWhile : public CStmt {
public:
    CDoWhile(CStmtPtr body, CExprPtr cond)
        : body_(std::move(body)), cond_(std::move(cond)) {}

    CStmt &body() { return *body_; }
    CExpr &cond() { return *cond_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    CStmtPtr body_;
    CExprPtr cond_;
};

class CFor : public CStmt {
public:
    CFor(CStmtPtr init, CExprPtr cond, CExprPtr step, CStmtPtr body)
        : init_(std::move(init)), cond_(std::move(cond)),
          step_(std::move(step)), body_(std::move(body)) {}

    CStmt *init() { return init_.get(); }
    CExpr *cond() { return cond_.get(); }
    CExpr *step() { return step_.get(); }
    CStmt &body() { return *body_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    CStmtPtr init_;
    CExprPtr cond_;
    CExprPtr step_;
    CStmtPtr body_;
};

class CSwitch : public CStmt {
public:
    CSwitch(CExprPtr cond, CStmtPtr body)
        : cond_(std::move(cond)), body_(std::move(body)) {}

    CExpr &cond() { return *cond_; }
    CStmt &body() { return *body_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    CExprPtr cond_;
    CStmtPtr body_;
};

class CCase : public CStmt {
public:
    CCase(CExprPtr value, CStmtPtr body)
        : value_(std::move(value)), body_(std::move(body)) {}

    bool isDefault() const { return value_ == nullptr; }
    CExpr *value() { return value_.get(); }
    CStmt &body() { return *body_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    CExprPtr value_;
    CStmtPtr body_;
};

class CBreak : public CStmt {
public:
    void accept(CVisitor &v) override { v.visit(*this); }
};

class CContinue : public CStmt {
public:
    void accept(CVisitor &v) override { v.visit(*this); }
};

class CReturn : public CStmt {
public:
    explicit CReturn(CExprPtr value) : value_(std::move(value)) {}

    CExpr *value() { return value_.get(); }
    CExprPtr &valueRef() { return value_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    CExprPtr value_;
};

class CGoto : public CStmt {
public:
    explicit CGoto(std::string label) : label_(std::move(label)) {}

    const std::string &label() const { return label_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    std::string label_;
};

class CLabel : public CStmt {
public:
    CLabel(std::string name, CStmtPtr body)
        : name_(std::move(name)), body_(std::move(body)) {}

    const std::string &name() const { return name_; }
    CStmt &body() { return *body_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    std::string name_;
    CStmtPtr body_;
};

class CBeyond : public CStmt {
public:
    CBeyond(std::string reason, std::vector<std::string> sourceLines)
        : reason_(std::move(reason)), lines_(std::move(sourceLines)) {}

    const std::string &reason() const { return reason_; }
    const std::vector<std::string> &lines() const { return lines_; }

    void accept(CVisitor &v) override { v.visit(*this); }

private:
    std::string reason_;
    std::vector<std::string> lines_;
};

class CFunctionDef {
public:
    CFunctionDef(std::string name, CTypePtr type, std::unique_ptr<CCompound> body,
                 bool isStatic)
        : name_(std::move(name)), type_(std::move(type)), body_(std::move(body)),
          isStatic_(isStatic) {}

    const std::string &name() const { return name_; }

    CType &type() { return *type_; }
    const CType &type() const { return *type_; }
    CCompound &body() { return *body_; }
    bool isStatic() const { return isStatic_; }

    std::size_t offset() const { return offset_; }
    void setOffset(std::size_t offset) { offset_ = offset; }

private:
    std::string name_;
    CTypePtr type_;
    std::unique_ptr<CCompound> body_;
    bool isStatic_;
    std::size_t offset_ = 0;
};

class CProgram {
public:
    struct Entry {
        bool isFunction;

        bool isMarker;
        std::size_t index;
    };

    void add(std::unique_ptr<CFunctionDef> fn) {
        order_.push_back(Entry{true, false, functions_.size()});
        functions_.push_back(std::move(fn));
    }
    void add(std::unique_ptr<CDeclaration> decl) {
        order_.push_back(Entry{false, false, declarations_.size()});
        declarations_.push_back(std::move(decl));
    }
    void addMarker(CStmtPtr marker) {
        order_.push_back(Entry{false, true, markers_.size()});
        markers_.push_back(std::move(marker));
    }

    std::vector<std::unique_ptr<CFunctionDef>> &functions() { return functions_; }
    std::vector<std::unique_ptr<CDeclaration>> &declarations() { return declarations_; }
    std::vector<CStmtPtr> &markers() { return markers_; }
    const std::vector<Entry> &order() const { return order_; }

private:
    std::vector<std::unique_ptr<CFunctionDef>> functions_;
    std::vector<std::unique_ptr<CDeclaration>> declarations_;
    std::vector<CStmtPtr> markers_;
    std::vector<Entry> order_;
};

}

#endif
