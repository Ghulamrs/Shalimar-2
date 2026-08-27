#include "CAst.h"

namespace c2s {

namespace {

}

std::unique_ptr<CType> CType::clone() const {
    std::unique_ptr<CType> copy(new CType(kind_));
    copy->isUnsigned_ = isUnsigned_;
    copy->isSignedExplicit_ = isSignedExplicit_;
    copy->isShort_ = isShort_;
    copy->isLong_ = isLong_;
    copy->isConst_ = isConst_;
    copy->isVolatile_ = isVolatile_;
    copy->isVariadic_ = isVariadic_;
    copy->isProtoVoid_ = isProtoVoid_;
    copy->hasMemberList_ = hasMemberList_;
    copy->tag_ = tag_;
    if (base_ != nullptr) copy->base_ = base_->clone();
    for (std::size_t i = 0; i < params_.size(); ++i) {
        Param param;
        param.name = params_[i].name;
        if (params_[i].type != nullptr) param.type = params_[i].type->clone();
        copy->params_.push_back(std::move(param));
    }
    for (std::size_t i = 0; i < members_.size(); ++i) {
        Member member;
        member.name = members_[i].name;
        if (members_[i].type != nullptr) member.type = members_[i].type->clone();
        copy->members_.push_back(std::move(member));
    }
    for (std::size_t i = 0; i < enumerators_.size(); ++i) {
        Enumerator e;
        e.name = enumerators_[i].name;
        copy->enumerators_.push_back(std::move(e));
    }

    return copy;
}

std::string CType::describe() const {
    std::string quals;
    if (isConst_) quals += "const ";
    if (isVolatile_) quals += "volatile ";

    switch (kind_) {
        case Kind::Void:   return quals + "void";
        case Kind::Char:
        case Kind::Int:
        case Kind::Float:
        case Kind::Double: {
            std::string name;
            if (isSignedExplicit_) name += "signed ";
            if (isUnsigned_) name += "unsigned ";
            if (isShort_) name += "short ";
            if (isLong_) name += "long ";
            switch (kind_) {
                case Kind::Char:   name += "char"; break;
                case Kind::Int:    name += "int"; break;
                case Kind::Float:  name += "float"; break;
                case Kind::Double: name += "double"; break;
                default: break;
            }
            return quals + name;
        }
        case Kind::Pointer:
            return quals + (base_ != nullptr ? base_->describe() : "?") + " *";
        case Kind::Array:
            return (base_ != nullptr ? base_->describe() : "?") +
                   (lengthText_ != nullptr ? " [n]" : " []");
        case Kind::Function:
            return (base_ != nullptr ? base_->describe() : "?") + " (...)";
        case Kind::Struct:
            return quals + "struct" + (tag_.empty() ? "" : " " + tag_);
        case Kind::Union:
            return quals + "union" + (tag_.empty() ? "" : " " + tag_);
        case Kind::Enum:
            return quals + "enum" + (tag_.empty() ? "" : " " + tag_);
        case Kind::Named:
            return quals + tag_;
    }
    return "?";
}

}
