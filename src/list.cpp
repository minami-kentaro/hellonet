#include "list.h"

HNetList::HNetList()
{
    clear();
}

HNetList::~HNetList()
{}

HNetListNode* HNetList::front()
{
    return m_Sentinel.next;
}

HNetListNode* HNetList::back()
{
    return m_Sentinel.prev;
}

void HNetList::push_back(HNetListNode* pNode)
{
    insert(end(), pNode);
}

void HNetList::push_back(HNetListNode* pFirst, HNetListNode* pLast)
{
    if (pFirst == nullptr || pLast == nullptr) {
        return;
    }
    pFirst->prev->next = pLast->next;
    pLast->next->prev = pFirst->prev;
    pFirst->prev = m_Sentinel.prev;
    pLast->next = &m_Sentinel;
    pFirst->prev->next = pFirst;
    m_Sentinel.prev = pLast;
}

void HNetList::push_front(HNetListNode* pNode)
{
    insert(begin(), pNode);
}

void HNetList::clear()
{
    m_Sentinel.next = &m_Sentinel;
    m_Sentinel.prev = &m_Sentinel;
}

bool HNetList::empty()
{
    return begin() == end();
}

void HNetList::insert(HNetListNode* pPos, HNetListNode* pNode)
{
    if (pPos != nullptr && pNode != nullptr) {
        pNode->prev = pPos->prev;
        pNode->next = pPos;
        pNode->prev->next = pNode;
        pPos->prev = pNode;
    }
}

HNetListNode* HNetList::remove(HNetListNode* pNode)
{
    if (pNode != nullptr) {
        pNode->prev->next = pNode->next;
        pNode->next->prev = pNode->prev;
    }
    return pNode;
}

HNetListNode* HNetList::begin()
{
    return m_Sentinel.next;
}

HNetListNode* HNetList::end()
{
    return &m_Sentinel;
}
