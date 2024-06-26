#include "gc.h"
#include "compile.h"
#include "obj_list.h"
#include "obj_range.h"
#if DEBUG
   #include "debug.h"
   #include <time.h>
#endif

//标灰obj:即把obj收集到数组vm->grays.grayObjects
void GrayObject(VM* vm, ObjHeader* obj)
{
   //如果isDark为true表示为黑色,说明已经可达,直接返回
   if (obj == NULL || obj->isdark) return;

   //标记为可达
   obj->isdark = true; 

   //若超过了容量就扩容
   if (vm->grays.count >= vm->grays.capacity) {
      vm->grays.capacity = vm->grays.count * 2;
      vm->grays.grayObjects = 
	 (ObjHeader**)realloc(vm->grays.grayObjects, vm->grays.capacity * sizeof(ObjHeader*));
   }

   //把obj添加到数组grayObjects
   vm->grays.grayObjects[vm->grays.count++] = obj;
}

//标灰value
void GrayValue(VM* vm, Value value)
{
   //只有对象才需要标记
   if (!VALUE_IS_OBJ(value)) {
      return;
   }
   GrayObject(vm, VALUE_TO_OBJ(value));
}

//标灰buffer->datas中的value
static void GrayBuffer(VM* vm, ValueBuffer* buffer)
{
   uint32_t idx = 0;
   while (idx < buffer->count) {
      GrayValue(vm, buffer->datas[idx]);
      idx++;
   }
}

//标黑class
static void BlackClass(VM* vm, Class* class) {
   //标灰meta类
   GrayObject(vm, (ObjHeader*)class->objHeader.class);

   //标灰父类
   GrayObject(vm, (ObjHeader*)class->superClass);

   //标灰方法
   uint32_t idx = 0;
   while (idx < class->methods.count) {
        if (class->methods.datas[idx].type == MT_SCRIPT) {
	        GrayObject(vm, (ObjHeader*)class->methods.datas[idx].obj);
        }
        idx++;
   }

   //标灰类名
    GrayObject(vm, (ObjHeader*)class->name);

   //累计类大小
   vm->allocatedBytes += sizeof(Class);
   vm->allocatedBytes += sizeof(Method) * class->methods.capacity;
}

//标灰闭包
static void BlackClosure(VM* vm, ObjClosure* objClosure) {
   //标灰闭包中的函数
   GrayObject(vm, (ObjHeader*)objClosure->fn);

   //标灰包中的upvalue
   uint32_t idx = 0;
   while (idx < objClosure->fn->upvalueNum) {
        GrayObject(vm, (ObjHeader*)objClosure->upvalues[idx]);
        idx++;
   }

   //累计闭包大小
   vm->allocatedBytes += sizeof(ObjClosure);
   vm->allocatedBytes += sizeof(ObjUpvalue*) * objClosure->fn->upvalueNum;
}

//标黑objThread
static void BlackThread(VM* vm, ObjThread* objThread) {
   //标灰frame
   uint32_t idx = 0;
   while (idx < objThread->usedFrameNum) {
      GrayObject(vm, (ObjHeader*)objThread->frames[idx].closure);
      idx++;
   }

   //标灰运行时栈中每个slot
   Value* slot =  objThread->stack;
   while (slot < objThread->esp) {
      GrayValue(vm, *slot);
      slot++; 
   }

   //标灰本线程中所有的upvalue
   ObjUpvalue* upvalue = objThread->openUpvalues;
   while (upvalue != NULL) {
      GrayObject(vm, (ObjHeader*)upvalue);
      upvalue = upvalue->next;
   }

   //标灰caller
   GrayObject(vm, (ObjHeader*)objThread->caller);
   GrayValue(vm, objThread->errorObj);

   //累计线程大小
   vm->allocatedBytes += sizeof(ObjThread);
   vm->allocatedBytes += objThread->frameCapacity * sizeof(Frame);
   vm->allocatedBytes += objThread->stackCapacity * sizeof(Value);
}

//标黑fn
static void BlackFn(VM* vm, ObjFn* fn) {
   //标灰常量
   GrayBuffer(vm, &fn->constants);

   //累计Objfn的空间
   vm->allocatedBytes += sizeof(ObjFn);
   vm->allocatedBytes += sizeof(uint8_t) * fn->instructStream.capacity;
   vm->allocatedBytes += sizeof(Value) * fn->constants.capacity;
  
#if DEBUG  
   //再加上debug信息占用的内存
   vm->allocatedBytes += sizeof(Int) * fn->instrStream.capacity;
#endif  
}

//标黑objInstance
static void BlackInstance(VM* vm, ObjInstance* objInstance) {
   //标灰元类
   GrayObject(vm, (ObjHeader*)objInstance->objHeader.class);

   //标灰实例中所有域,域的个数在class->fieldNum
   uint32_t idx = 0;
   while (idx < objInstance->objHeader.class->fieldNum) {
      GrayValue(vm, objInstance->fields[idx]);
      idx++;
   }

   //累计objInstance空间
   vm->allocatedBytes += sizeof(ObjInstance);
   vm->allocatedBytes += sizeof(Value) * objInstance->objHeader.class->fieldNum;
}

//标黑objList
static void BlackList(VM* vm, ObjList* objList) {
   //标灰list的elements
   GrayBuffer(vm, &objList->elements);

   //累计objList大小
   vm->allocatedBytes += sizeof(ObjList);
   vm->allocatedBytes += sizeof(Value) * objList->elements.capacity;
}

//标黑objMap
static void BlackMap(VM* vm, ObjMap* objMap) {
   //标灰所有entry
   uint32_t idx = 0;
   while (idx < objMap->capacity) {
      Entry* entry = &objMap->entries[idx];
      //跳过无效的entry
      if (!VALUE_IS_UNDEFINED(entry->key)) {
        GrayValue(vm, entry->key);
        GrayValue(vm, entry->value);
      }
      idx++;
   }

   //累计ObjMap大小
   vm->allocatedBytes += sizeof(ObjMap);
   vm->allocatedBytes += sizeof(Entry) * objMap->capacity;
}

//标黑objModule
static void BlackModule(VM* vm, ObjModule* objModule) {
   //标灰模块中所有模块变量
   uint32_t idx = 0;
   while (idx < objModule->moduleVarValue.count) {
      GrayValue(vm, objModule->moduleVarValue.datas[idx]);
      idx++;
   }

   //标灰模块名
   GrayObject(vm, (ObjHeader*)objModule->name);

   //累计ObjModule大小
   vm->allocatedBytes += sizeof(ObjModule);
   vm->allocatedBytes += sizeof(String) * objModule->moduleVarName.capacity;
   vm->allocatedBytes += sizeof(Value) * objModule->moduleVarValue.capacity;
}

//标黑range
static void BlackRange(VM* vm) {
   //ObjRange中没有大数据,只有from和to,
   //其空间属于sizeof(ObjRange),因此不用额外标记
   vm->allocatedBytes += sizeof(ObjRange);
}

//标黑objString
static void BlackString(VM* vm, ObjString* objString) {
   //累计ObjString空间 +1是结尾的'\0'
   vm->allocatedBytes += sizeof(ObjString) + objString->value.length + 1;
}

//标黑objUpvalue
static void BlackUpvalue(VM* vm, ObjUpvalue* objUpvalue) {
   //标灰objUpvalue的closedUpvalue
   GrayValue(vm, objUpvalue->closedUpvalue);

   //累计objUpvalue大小
   vm->allocatedBytes += sizeof(ObjUpvalue);
}

//标黑obj
static void BlackObject(VM* vm, ObjHeader* obj) {
#ifdef DEBUG
   printf("mark ");
   DumpValue(OBJ_TO_VALUE(obj));
   printf(" @ %p\n", obj);
#endif
//根据对象类型分别标黑
   switch (obj->type) {
        case OT_CLASS:
	        BlackClass(vm, (Class*)obj);
	        break;
        case OT_CLOSURE:
            BlackClosure(vm, (ObjClosure*)obj);
            break;
        case OT_THREAD:
            BlackThread(vm, (ObjThread*)obj);
            break;
        case OT_FUNCTION:
            BlackFn(vm, (ObjFn*)obj);
            break;
        case OT_INSTANCE:
            BlackInstance(vm, (ObjInstance*)obj);
            break;
        case OT_LIST:
            BlackList(vm, (ObjList*)obj);
            break;
        case OT_MAP:
            BlackMap(vm, (ObjMap*)obj);
            break;
        case OT_MODULE:
            BlackModule(vm, (ObjModule*)obj);
            break;
        case OT_RANGE:
            BlackRange(vm);
            break;
        case OT_STRING:
            BlackString(vm, (ObjString*)obj);
            break;
        case OT_UPVALUE: 
            BlackUpvalue(vm, (ObjUpvalue*)obj);
            break;
   }
}

//标黑那些已经标灰的对象,即保留那些标灰的对象
static void BlackObjectInGray(VM* vm) {
//所有要保留的对象都已经收集到了vm->grays.grayObjects中,
//现在逐一标黑
   while (vm->grays.count > 0) {
      ObjHeader* objHeader = vm->grays.grayObjects[--vm->grays.count];
      BlackObject(vm, objHeader);
   }
}

//释放obj自身及其占用的内存
void FreeObject(VM* vm, ObjHeader* obj)
{
#ifdef DEBUG 
   printf("free ");
   DumpValue(OBJ_TO_VALUE(obj));
   printf(" @ %p\n", obj);
#endif

    //根据对象类型分别处理
    switch (obj->type) { 
        case OT_CLASS:
	        MethodBufferClear(vm, &((Class*)obj)->methods);
	        break;

        case OT_THREAD: {
            ObjThread* objThread = (ObjThread*)obj;
            DEALLOCATE(vm, objThread->frames);
            DEALLOCATE(vm, objThread->stack);
            break;
        }
        case OT_FUNCTION: {
	        ObjFn* fn = (ObjFn*)obj;
            ValueBufferClear(vm, &fn->constants);
            ByteBufferClear(vm, &fn->instructStream);
            #if DEBUG
            IntBufferClear(vm, &fn->debug->lineNo);
            DEALLOCATE(vm, fn->debug->fnName);
            DEALLOCATE(vm, fn->debug);
            #endif
            break;
        }
        case OT_LIST:
            ValueBufferClear(vm, &((ObjList*)obj)->elements);
            break;
        case OT_MAP:
            DEALLOCATE(vm, ((ObjMap*)obj)->entries);
            break;
        case OT_MODULE:
            StringBufferClear(vm, &((ObjModule*)obj)->moduleVarName);
            ValueBufferClear(vm, &((ObjModule*)obj)->moduleVarValue);
            break;
        case OT_STRING:
        case OT_RANGE:
        case OT_CLOSURE:
        case OT_INSTANCE:
        case OT_UPVALUE:
	        break;
   }

   //最后再释放自己
   DEALLOCATE(vm, obj);
}

//立即运行垃圾回收器去释放未用的内存
void StartGC(VM* vm)
{
#ifdef DEBUG 
   double startTime = (double)clock() / CLOCKS_PER_SEC;
   uint32_t before = vm->allocatedBytes;
   printf("-- gc  before:%d   nextGC:%d  vm:%p  --\n", before, vm->config.nextGC, vm);
#endif
    // 一 标记阶段:标记需要保留的对象

   //将allocatedBytes置0便于精确统计回收后的总分配内存大小
   vm->allocatedBytes = 0;

  //allModules不能被释放
   GrayObject(vm, (ObjHeader*)vm->allModules);

   //标灰tmpRoots数组中的对象(不可达但是不想被回收,白名单)
   uint32_t idx = 0;
   while (idx < vm->tmpRootNum) {
      GrayObject(vm, vm->tmpRoots[idx]);
      idx++;
   }

   //标灰当前线程,不能被回收
   GrayObject(vm, (ObjHeader*)vm->curThread);

   //编译过程中若申请的内存过高就标灰编译单元
//    if (vm->curParser != NULL) {
//         ASSERT(vm->curParser->curCompileUnit != NULL, "grayCompileUnit only be called while compiling!");
//         GrayCompileUnit(vm, vm->curParser->curCompileUnit);  
//    }

   //置黑所有灰对象(保留的对象)
   BlackObjectInGray(vm);  

    // 二 清扫阶段:回收白对象(垃圾对象)

   ObjHeader** obj = &vm->allObjects;
   while (*obj != NULL) { 
        //回收白对象
        if (!((*obj)->isdark)) {
	        ObjHeader* unreached = *obj;
	        *obj = unreached->next;
	        FreeObject(vm, unreached);
        } else {
	        //如果已经是黑对象,为了下一次gc重新判定,
	        //现在将其恢复为未标记状态,避免永远不被回收
	        (*obj)->isdark = false;
	        obj = &(*obj)->next;
      }
   }

    //更新下一次触发gc的阀值
    vm->config.nextGC = vm->allocatedBytes * vm->config.heapGrowthFactor;
    if (vm->config.nextGC < vm->config.minHeapSize) {
        vm->config.nextGC = vm->config.minHeapSize;
    }

#ifdef DEBUG
   double elapsed = ((double)clock() / CLOCKS_PER_SEC) - startTime;
   printf("GC %lu before, %lu after (%lu collected), next at %lu. take %.3fs.\n",
	 (unsigned long)before,
	 (unsigned long)vm->allocatedBytes,
	 (unsigned long)(before - vm->allocatedBytes),
	 (unsigned long)vm->config.nextGC,
	 elapsed);
#endif
}