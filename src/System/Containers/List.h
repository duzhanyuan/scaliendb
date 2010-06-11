#ifndef LIST_H
#define LIST_H

#include <stddef.h>
#include <assert.h>

template<class T> struct ListNode; // forward
template<class T> class SortedList; // for friend

/*
 * List is a generic doubly-linked list
 */

template<class T>
class List
{
public:
	List();
	~List();
	
	void			Prepend(T &t);
	void			Append(T &t);
	T*				Remove(T* t);	
	bool			Remove(T &t);
	void			Clear();
	
	T*				Head() const;
	T*				Tail() const;
	int				Length() const;
	
	T*				Next(T* t) const;
	T*				Prev(T* t) const;

private:
	ListNode<T>*	head;
	ListNode<T>*	tail;
	int				length;

	friend class SortedList<T>;
};

/****************************************************************************/

template<class T>
struct ListNode
{
	T           	data;
	
	ListNode<T>*	next;
	ListNode<T>*	prev;
	
	void Init(T &data_, ListNode<T>* next_, ListNode<T>* prev_)
	{
		data = data_;
		next = next_;
		prev = prev_;
	}
};

template<class T>
List<T>::List()
{
	head = NULL;
	tail = NULL;
	length = 0;
}

template<class T>
List<T>::~List()
{
	Clear();
}

template<class T>
void List<T>::Prepend(T &t)
{
	ListNode<T>* node;
	
	node = new ListNode<T>;
	node->Init(t, head, NULL);
	
	if (head != NULL)
		head->prev = node;
	head = node;
	length++;
	
	if (tail == NULL)
		tail = node;
}

template<class T>
void List<T>::Append(T &t)
{
	ListNode<T>* node;
	
	node = new ListNode<T>;
	node->Init(t, NULL, tail);
	
	if (tail != NULL)
		tail->next = node;
	tail = node;
	length++;
	
	if (head == NULL)
		head = node;
}

template<class T>
T* List<T>::Remove(T* t)
{
	ListNode<T>* node;
	T* ret;
	
	node = (ListNode<T>*) t;
	if (head == node)
		head = node->next;
	else
		node->prev->next = node->next;
	
	if (tail == node)
		tail = node->prev;
	else
		node->next->prev = node->prev;
	
	length--;
	
	ret = NULL;
	if (node->next != NULL)
		ret = &node->next->data;
	
	delete node;
	return ret;
}

template<class T>
bool List<T>::Remove(T &t)
{
	T* it;
	
	for (it = Head(); it != NULL; it = Next(it))
	{
		if (*it == t)
		{
			Remove(it);
			return true;
		}
	}
	
	// not found
	return false;
}

template<class T>
void List<T>::Clear()
{
	ListNode<T> *t, *n;
	
	t = head;
	while (t)
	{
		n = t->next;
		delete t;
		t = n;
		length--;
	}
	
	head = tail = NULL;
	
	assert(length == 0);
}

template<class T>
T* List<T>::Head() const
{
	if (head == NULL)
		return NULL;
	else
		return &head->data;
}

template<class T>
T* List<T>::Tail() const
{
	if (tail == NULL)
		return NULL;
	else
		return &tail->data;
}

template<class T>
int List<T>::Length() const
{
	return length;
}

template<class T>
T* List<T>::Next(T* t) const
{
	ListNode<T>* node = (ListNode<T>*) t;
	if (node->next == NULL)
		return NULL;
	else
		return &node->next->data;
}

template<class T>
T* List<T>::Prev(T* t) const
{
	ListNode<T>* node = (ListNode<T>*) t;
	if (node->prev == NULL)
		return NULL;
	else
		return &node->prev->data;
}

#endif
