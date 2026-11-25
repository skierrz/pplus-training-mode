// Destructor chain for file-scope objects, as required by the CodeWarrior C++ ABI.
//
// For every file-scope object with a non-trivial destructor, MWCC's static
// initializer calls __register_global_object() with a 12-byte block of static
// storage, which we use as a list node. __destroy_global_chain() then runs the
// destructors in reverse order of construction.
//
// On unload, _epilog walks .dtors. When anything was registered, the linker
// itself emits a .dtors entry pointing at __destroy_global_chain; otherwise
// .dtors holds only its null terminator.

#include <stddef.h>

extern "C" {

struct GlobalChainNode {
    GlobalChainNode* next;
    void* destructor;
    void* object;
};

// MWCC destructors take (this, flag); -1 destroys without freeing storage.
typedef void (*GlobalDtor)(void* object, int flag);

static GlobalChainNode* sChainHead = NULL;

void* __register_global_object(void* object, void* destructor, void* regmem)
{
    GlobalChainNode* node = static_cast<GlobalChainNode*>(regmem);
    node->object = object;
    node->destructor = destructor;
    node->next = sChainHead;
    sChainHead = node;
    return object;
}

void __destroy_global_chain()
{
    while (sChainHead != NULL)
    {
        GlobalChainNode* node = sChainHead;
        sChainHead = node->next;
        reinterpret_cast<GlobalDtor>(node->destructor)(node->object, -1);
    }
}

// mwldeppc refuses to link ("runtime sources ... need to be updated") unless
// this symbol is defined. It substitutes its own .dtors entry, so this one is
// deliberately not placed in .dtors; doing so yields a second, redundant entry.
void (*const __destroy_global_chain_reference)() = __destroy_global_chain;
}
