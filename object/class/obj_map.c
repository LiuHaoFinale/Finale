#include "obj_map.h"
#include "class.h"
#include "vm.h"
#include "obj_string.h"
#include "obj_range.h"

/**
 * @brief 创建新map对象
*/
ObjMap* NewObjMap(VM *vm)
{
    ObjMap *objMap = ALLOCATE(vm, ObjMap);
    InitObjheader(vm, &objMap->objHeader, OT_MAP, vm->mapClass);
    objMap->capacity = objMap->count = 0;
    objMap->entries = NULL;
    return objMap;
}

/**
 * @brief 计算数字的哈希码
*/
static uint32_t HashNum(double num)
{
    Bits64 bits64;
    bits64.num = num;
    return bits64.bits32[0] ^ bits64.bits32[1];
}

/**
 * @brief 计算对象的哈希码
*/
static uint32_t HashObj(ObjHeader *objHeader)
{
    switch (objHeader->type)
    {
        case OT_CLASS: // 计算class的哈希值
            return HashString(((Class *)objHeader)->name->value.start, ((Class *)objHeader)->name->value.length);
        case OT_RANGE:
            ObjRange *objRange = (ObjRange *)objHeader;
            return HashNum(objRange->from) ^ HashNum(objRange->to);
        case OT_STRING: // 对于字符串 直接返回其hash_code
            return ((ObjString *)objHeader)->hashCode;
        default:
            RUNTIME_ERROR("The hashable are objstring, objrange and class.");
    }
    return 0;
}

/**
 * @brief 根据value的类型调用相应的哈希函数
*/
static uint32_t HashValue(Value value)
{
    switch (value.valueType) {
        case VT_FALSE:
            return 0;
        case VT_NULL:
            return 1;
        case VT_NUM:
            return HashNum(value.num);
        case VT_TRUE:
            return 2;
        case VT_OBJ:
            return HashObj(value.objHeader);
        default:
            RUNTIME_ERROR("unsupport type hashed!");
    }
    return 0;
}

/**
 * @brief 在entries中添加entry，如果是新的key则返回true
*/
static boolean AddEntry(Entry *entries, uint32_t capacity, Value key, Value value)
{
    uint32_t index = HashValue(key) % capacity;
    // 开放定址法
    while (true) {
        if (entries[index].key.valueType == VT_UNDEFINED) {
            entries[index].key = key;
            entries[index].value = value;
            return true;
        }
        else if (valueIsEqual(entries[index].key, key)) {
            entries[index].value = value;
            return false;
        }
        index = (index + 1) % capacity;
    }
}

/**
 * @brief 使对象objMap的容量调整到new_capacity
*/
static void ResizeMap(VM *vm, ObjMap *objMap, uint32_t newCapacity)
{
    // 先建立一个新的entry数组
    Entry *newEntries = ALLOCATE_ARRAY(vm, Entry, newCapacity);
    uint32_t idx = 0;
    while (idx < newCapacity) {
        newEntries[idx].key = VT_TO_VALUE(VT_UNDEFINED);
        newEntries[idx].value =VT_TO_VALUE(VT_FALSE);
        idx ++;
    }
    if (objMap->capacity > 0) {
        Entry *entryArr = objMap->entries;
        idx = 0;
        while (idx < objMap->capacity) {
            if (entryArr[idx].key.valueType != VT_UNDEFINED) {
                AddEntry(newEntries, newCapacity, entryArr[idx].key, entryArr[idx].value);
            }
            idx ++;
        }
    }
    // 把老entry数组空间回收
    DEALLOCATE_ARRAY(vm, objMap->entries, objMap->count);
    objMap->entries = newEntries;
    objMap->capacity = newCapacity;
}

/**
 * @brief 在objMap中查找key对应的entry
*/
static Entry* FindEntry(ObjMap *objMap, Value key)
{
    if (objMap->capacity == 0) {
        return NULL;
    }
    // 用开放定址法探测
    uint32_t index = HashValue(key) % objMap->capacity;
    Entry *entry;
    while (true) {
        entry = &objMap->entries[index];
        if (ValueIsEqual(entry->key, key)) {
            return entry;
        }
        if (VALUE_IS_UNDEFINED(entry->key) && VALUE_IS_FALSE(entry->value)) {
            return NULL; // 未找到
        }
        index = (index + 1) % objMap->capacity;
    }
}

/**
 * @brief 在objMap中实现key和value的关联
*/
void MapSet(VM *vm, ObjMap *objMap, Value key, Value value)
{
    if (objMap->count + 1 > objMap->capacity * MAP_LOAD_PERCENT) {
        uint32_t newCapacity = objMap->capacity * CAPACITY_GROW_FACTOR;
        if (newCapacity < MIN_CAPACITY) {
            newCapacity = MIN_CAPACITY;
        }
        ResizeMap(vm, objMap, newCapacity);
    }
    
    if (AddEntry(objMap->entries, objMap->capacity, key, value)) {
        objMap->count ++;
    }
}

/**
 * @brief 从map查找key对应的value
*/
Value MapGet(ObjMap *objMap, Value key)
{
    Entry *entry = FindEntry(objMap, key);
    if (entry == NULL)
    {
        return VT_TO_VALUE(VT_UNDEFINED);
    }
    return entry->value;
}

/**
 * @brief 回收objMap.entries占用的空间
*/
void ClearMap(VM *vm, ObjMap *objMap)
{
    DEALLOCATE_ARRAY(vm, objMap->entries, objMap->count);
    objMap->entries = NULL;
    objMap->capacity = objMap->count = 0;
}

/**
 * @brief 删除objMap中的key
*/
Value RemoveKey(VM *vm, ObjMap *objMap, Value key)
{
    Entry *entry = FindEntry(objMap, key);
    if (entry == NULL)
    {
        return VT_TO_VALUE(VT_NULL);
    }

    // 设置开放定址的伪删除
    Value value = entry->value;
    entry->key = VT_TO_VALUE(VT_UNDEFINED);
    entry->value = VT_TO_VALUE(VT_TRUE);

    objMap->count --;
    if (objMap->count == 0) {
        ClearMap(vm, objMap);
    }
    else if (objMap->count < objMap->capacity / (CAPACITY_GROW_FACTOR) * MAP_LOAD_PERCENT && objMap->count > MIN_CAPACITY)
    {
        uint32_t newCapacity = objMap->capacity / CAPACITY_GROW_FACTOR;
        if (newCapacity < MIN_CAPACITY)
        {
            newCapacity = MIN_CAPACITY;
        }
        ResizeMap(vm, objMap, newCapacity);
    }
    return value;
}