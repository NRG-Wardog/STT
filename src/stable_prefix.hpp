#pragma once
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace heben {
// Append-only agreement for one utterance. Exact tokens preserve script and
// punctuation; no editor text is read, selected, replaced, or deleted.
class StablePrefix {
public:
    struct Result {std::wstring append;bool conflict=false;};
    explicit StablePrefix(unsigned agreement=3,unsigned holdWords=1)
        :agreement_(std::max(2u,agreement)),holdWords_(holdWords) {}

    Result Observe(const std::wstring &hypothesis,bool final) {
        auto tokens=Tokenize(hypothesis);
        if(!StartsWith(tokens,committed_)) {
            // If only boundary punctuation or ASCII case changed, preserve
            // already typed words and append the unambiguous remainder. The
            // caller still retains the full final text for explicit review.
            if(final && StartsWithEquivalent(tokens,committed_))
                return {Join(tokens,committed_.size(),tokens.size()),true};
            if(final)return {L"",true};
            previous_.clear();return {};
        }
        if(final) {
            auto output=Join(tokens,committed_.size(),tokens.size());
            committed_=std::move(tokens);previous_.clear();return {output,false};
        }
        previous_.push_back(tokens);
        if(previous_.size()>agreement_)previous_.erase(previous_.begin());
        if(previous_.size()<agreement_)return {};
        size_t length=previous_.front().size();
        for(size_t i=1;i<previous_.size();++i) {
            length=std::min(length,previous_[i].size());
            size_t j=0;while(j<length && previous_.front()[j]==previous_[i][j])++j;
            length=j;
        }
        length=length>holdWords_?length-holdWords_:0;
        if(length<=committed_.size())return {};
        auto output=Join(tokens,committed_.size(),length);
        committed_.assign(tokens.begin(),tokens.begin()+length);
        return {output,false};
    }
private:
    static std::vector<std::wstring> Tokenize(const std::wstring &s) {
        std::vector<std::wstring> result;size_t i=0;
        while(i<s.size()) {
            while(i<s.size() && std::iswspace(s[i]))++i;
            size_t start=i;while(i<s.size() && !std::iswspace(s[i]))++i;
            if(i>start)result.emplace_back(s.substr(start,i-start));
        }
        return result;
    }
    static bool StartsWith(const std::vector<std::wstring>&text,const std::vector<std::wstring>&prefix) {
        return text.size()>=prefix.size() && std::equal(prefix.begin(),prefix.end(),text.begin());
    }
    static std::wstring Comparable(const std::wstring &token) {
        auto punct=[](wchar_t c){return c==L'.'||c==L','||c==L'!'||c==L'?'||c==L';'||c==L':'||
            c==L'('||c==L')'||c==L'['||c==L']'||c==L'{'||c==L'}'||c==L'"'||c==0x05c3;};
        size_t a=0,b=token.size();while(a<b && punct(token[a]))++a;while(b>a && punct(token[b-1]))--b;
        std::wstring result=token.substr(a,b-a);
        for(auto &c:result)if(c>=L'A' && c<=L'Z')c=static_cast<wchar_t>(c+(L'a'-L'A'));
        return result;
    }
    static bool StartsWithEquivalent(const std::vector<std::wstring>&text,const std::vector<std::wstring>&prefix) {
        if(text.size()<prefix.size())return false;
        for(size_t i=0;i<prefix.size();++i) {
            auto a=Comparable(text[i]),b=Comparable(prefix[i]);
            if(a.empty() || a!=b)return false;
        }
        return true;
    }
    static std::wstring Join(const std::vector<std::wstring>&parts,size_t first,size_t last) {
        std::wstring out;
        for(size_t i=first;i<last;++i){if(!out.empty())out.push_back(L' ');out+=parts[i];}
        return out;
    }
    unsigned agreement_,holdWords_;
    std::vector<std::vector<std::wstring>> previous_;
    std::vector<std::wstring> committed_;
};
}
