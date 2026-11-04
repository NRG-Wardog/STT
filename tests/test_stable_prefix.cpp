#include "../src/stable_prefix.hpp"
#include <cstdio>

int main() {
    heben::StablePrefix p(3,1);
    if(!p.Observe(L"שלום עולם",false).append.empty())return 1;
    if(!p.Observe(L"שלום עולם טוב",false).append.empty())return 2;
    auto first=p.Observe(L"שלום עולם טוב מאוד",false);
    if(first.append!=L"שלום" || first.conflict)return 3;
    auto second=p.Observe(L"שלום עולם טוב מאוד",false);
    if(second.append!=L"עולם" || second.conflict)return 4;
    if(p.Observe(L"שלום עולם טוב מאוד",false).append!=L"טוב")return 11;
    auto tail=p.Observe(L"שלום עולם טוב מאוד!",true);
    if(tail.append!=L"מאוד!" || tail.conflict)return 5;
    heben::StablePrefix q(2,1);
    q.Observe(L"hello world",false);
    if(q.Observe(L"hello world again",false).append!=L"hello")return 6;
    if(!q.Observe(L"yellow world again",false).append.empty())return 7;
    auto conflict=q.Observe(L"yellow world again",true);
    if(!conflict.conflict || !conflict.append.empty())return 8;
    heben::StablePrefix mixed(2,1);
    mixed.Observe(L"שלום hello, world",false);
    auto m=mixed.Observe(L"שלום hello, world again",false);
    if(m.append!=L"שלום hello,")return 9;
    if(mixed.Observe(L"שלום hello, world again",true).append!=L"world again")return 10;
    heben::StablePrefix punctuation(2,1);
    punctuation.Observe(L"Hello shuttle next",false);
    if(punctuation.Observe(L"Hello shuttle next today",false).append!=L"Hello shuttle")return 12;
    auto changed=punctuation.Observe(L"hello shuttle, next today",true);
    if(!changed.conflict || changed.append!=L"next today")return 13;
    std::puts("PASS_STABLE_PREFIX: append-only agreement, punctuation, mixed script, final flush and conflict");
}
