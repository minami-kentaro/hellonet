#pragma once

#include "types.h"

struct HNetListNode
{
    HNetListNode* next;
    HNetListNode* prev;
};

class HNetList final
{
public:
    HNetList();
    ~HNetList();

    HNetListNode* front();
    HNetListNode* back();
    void push_back(HNetListNode* pNode);
    void push_back(HNetListNode* pFirst, HNetListNode* pLast);
    void push_front(HNetListNode* pNode);
    void clear();
    bool empty();
    static HNetListNode* remove(HNetListNode* pNode);

    HNetListNode* begin();
    HNetListNode* end();

private:
    HNetListNode m_Sentinel{};
};
