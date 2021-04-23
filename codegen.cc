/* File: codegen.cc
 * ----------------
 * Implementation for the CodeGenerator class. The methods don't do anything
 * too fancy, mostly just create objects of the various Tac instruction
 * classes and append them to the list.
 */

#include "ast_decl.h"
#include "codegen.h"
#include "errors.h"
#include "mips.h"
#include "tac.h"

void CodeGenerator::Genlabels()
{
    // label to instruction number
    labels = new Hashtable<int>;
    for(int i = 0; i < code->NumElements(); ++i) {
        Label *lb = dynamic_cast<Label*>(code->Nth(i));
        if(lb)
            labels->Enter(lb->GetLabel(), i);
    }
}

void CodeGenerator::GenSucc()
{
    int len = code->NumElements();
    succ = new std::unordered_set<int>[len];

    for (int i = 0; i < len - 1; i++) {
        Instruction *ins = code->Nth(i);
        if (curFxn) {
            if (ins->IsEndFunc()) {
                curFxn = NULL; // EndFunc has no successor.
            } else if (ins->IsGoto()) {
                Goto *g = static_cast<Goto*>(ins);
                succ[i].insert(labels->Lookup(g->GetLabel()));
            } else if (ins->IsIfz()) {
                IfZ *g = static_cast<IfZ*>(ins);
                succ[i].insert(labels->Lookup(g->GetLabel()));
                succ[i].insert(i + 1);
            } else
                // by default, next instruction is the succ
                succ[i].insert(i + 1);
        } else if (ins->IsBeginFunc()) {
            curFxn = static_cast<BeginFunc*>(ins);
            succ[i].insert(i + 1);
        }
    }
    curFxn = NULL;
}

void Set_Union(std::vector<Location*> &set1, std::vector<Location*> &set2, std::vector<Location*> &result)
{
    result.clear();
    result.resize(set1.size()+set2.size());
    std::vector<Location*>::iterator it;
    it = std::set_union(set1.begin(), set1.end(), set2.begin(), set2.end(), result.begin());
    result.resize(it-result.begin());
}

/*
template<typename T>
std::vector<T> set_diff(std::set<T> const &a, std::set<T> const &b) {
    std::vector v<T>;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(), v.begin());
    return v;
}
*/
void Set_Diff(std::vector<Location *> &set1, std::vector<Location *> &set2, std::vector<Location *> &result)
{
    result.clear();
    result.resize(set1.size());
    std::vector<Location *>::iterator it;
    it = std::set_difference(set1.begin(), set1.end(), set2.begin(), set2.end(), result.begin());
    result.resize(it-result.begin());
}

void CodeGenerator::LiveVarAnalysis(int start, int end)
{   
    bool changed = true;
    while(changed){
      changed = false;
      for (int i = start; i <= end; i++) {
          // OUT[tac] = Union(IN[SUCC(tac)])
          std::vector<Location*> outTemp, temp;
          for (int s: succ[i]) {
              Set_Union(outTemp, in[s], temp);
              outTemp.assign(temp.begin(), temp.end());
          }
          out[i].assign(outTemp.begin(), outTemp.end());

          // IN' = OUT - KILL + GEN
          std::vector<Location*> curIn, gen;
          Instruction *ins = code->Nth(i);
          // build kill set if not
          if (kill[i].empty()) {
              Location *dst = ins->Getlhs();
              if (dst != NULL)
                  kill[i].push_back(dst);
          }
          Location *rhs1 = ins->Getrhs1();
          if (rhs1 != NULL)
              gen.push_back(rhs1);
          Location *rhs2 = ins->Getrhs2();
          if (rhs2 != NULL)
              gen.push_back(rhs2);
          std::sort(gen.begin(), gen.end());
          Set_Diff(out[i], kill[i], temp);
          Set_Union(temp, gen, curIn);
          if (curIn != in[i]) {
              in[i].assign(curIn.begin(), curIn.end());
              changed = true;
          }
      }
    }
}

void CodeGenerator::LivenessAnalysis()
{
    int len = code->NumElements();
    in = new std::vector<Location*>[len];
    out = new std::vector<Location*>[len];
    kill = new std::vector<Location*>[len];

    int start = 0;
    // find beginfunc, mark it in start; find endfunc, call LiveVarAnalysis()
    for(int i = 0; i < len; i++) {
        Instruction *ins = code->Nth(i);
        if (curFxn != NULL && ins->IsEndFunc()) {
            curFxn = NULL;
            LiveVarAnalysis(start, i);
        } else if (curFxn == NULL && ins->IsBeginFunc()) {
            curFxn = static_cast<BeginFunc*>(ins);
            start = i;
        }
    }
    curFxn = NULL;
}

InterferenceGraph *CodeGenerator::BuildGraph(int start, int end)
{
    
    InterferenceGraph *graph = new InterferenceGraph;
    for (int i = start; i <= end; i++) {
        //Instruction *ins = code->Nth(i);
        std::vector<Location*> commonSet;
        Set_Union(out[i], kill[i], commonSet);
        // for each pair of nodes in commonSet add edges between them
        int len = commonSet.size();
        for (int j = 0; j < len; j++) {
            for (int k = 0; k < len; k++) {
                auto it = graph->find(commonSet[j]);
                if (it == graph->end()) {
                    std::list<Location*> edgeList = {commonSet[k]};
                    graph->insert(std::make_pair(commonSet[j], edgeList));
                } else {
                    auto found = std::find(it->second.begin(), it->second.end(), commonSet[k]);
                    if (found == it->second.end())
                        it->second.push_back(commonSet[k]);
                }
            }
        }
    }
    return graph; 
}

void CodeGenerator::RegAllocation(InterferenceGraph *graph, int start, int end)
{ 
  
    std::unordered_map<Location*, std::list<Location*>> removed;
    std::stack<Location*> nodeStack;

    // 1. tear down the graph and add to the stack
    while (graph->size() > 0) {
        auto node = graph->end();
        int maxEdge = 0;
        for (auto it = graph->begin(); it != graph->end(); it++) {
            int nodeNo = 0; // number of edges not removed
            for (auto loc: it->second) {
              // not removed
              if (removed.find(loc) == removed.end())
                  nodeNo++;
            }
            if (nodeNo < Mips::NumGeneralPurposeRegs) {
              // < 18: this is it!
                node = it;
                break;
            } else if (nodeNo > maxEdge) {
              // have to spill: find the one with most edges
                node = it;
                maxEdge = nodeNo;
            }
        }
        // push the node to stack and remove all relevant edges.
        removed.insert(std::make_pair(node->first, node->second));
        nodeStack.push(node->first);
        graph->erase(node);
    }

    // 2. reconstruct the graph
    std::unordered_set<int> regsSet;
    int reg0 = static_cast<int>(Mips::t0);
    for(int i = reg0; i < reg0 + Mips::NumGeneralPurposeRegs; i++)
        regsSet.insert(i);
    while (!nodeStack.empty()) {
        Location *node = nodeStack.top();
        nodeStack.pop();
        auto it = removed.find(node);
        graph->insert(std::make_pair(node, it->second));

        std::unordered_set<int> temp(regsSet);
        // check if there is any free reg left
        for (auto loc: it->second) {
            auto neighbour = graph->find(loc);
            if (neighbour != graph->end() && neighbour->first->GetRegister()) {
                Mips::Register reg = neighbour->first->GetRegister();
                auto occupied = temp.find(static_cast<int>(reg));
                if (occupied != temp.end())
                    temp.erase(occupied);
            }
        }
        // allocate register
        if (!temp.empty())
            node->SetRegister(static_cast<Mips::Register>(*temp.begin()));
        else
            node->SetRegister(Mips::zero);
        removed.erase(it);
    }
}

void CodeGenerator::GraphColoring()
{
    int start = 0;
    // Locate functions and do register allocation inside each of them
    for (int i = 0; i < code->NumElements(); ++i) {
        Instruction *ins = code->Nth(i);
        if (curFxn != NULL && ins->IsEndFunc()) {
            curFxn = NULL;
            InterferenceGraph *graph = BuildGraph(start, i);
            RegAllocation(graph, start, i);
            delete graph;
        } else if (curFxn == NULL && ins->IsBeginFunc()) {
            curFxn = static_cast<BeginFunc*>(ins);
            start = i;
        }
    }
    curFxn = NULL;
}

void CodeGenerator::GenRegMap()
{
    for (int i = 0; i < code->NumElements(); i++) {
        Instruction *ins = code->Nth(i);
        memset(ins->register_map, 0, sizeof(ins->register_map));
        for (Location *loc: in[i]) {
            int r = static_cast<int>(loc->GetRegister());
            int reg0 = static_cast<int>(Mips::t0);
            ins->register_map[r-reg0] = loc;
        }
    }
}

CodeGenerator::CodeGenerator(): labels(NULL), succ(NULL), in(NULL), out(NULL), kill(NULL){    
    code = new List<Instruction*>();
    curGlobalOffset = 0;
}

char *CodeGenerator::NewLabel()
{
    static int nextLabelNum = 0;
    char temp[10];
    sprintf(temp, "_L%d", nextLabelNum++);
    return strdup(temp);
}


Location *CodeGenerator::GenTempVar()
{
    static int nextTempNum;
    char temp[10];
    Location *result = NULL;
    sprintf(temp, "_tmp%d", nextTempNum++);
    return GenLocalVariable(temp);
}


Location *CodeGenerator::GenLocalVariable(const char *varName)
{
    curStackOffset -= VarSize;
    Location *loc = new Location(fpRelative, curStackOffset+4, varName);
    return loc;
}

Location *CodeGenerator::GenGlobalVariable(const char *varName)
{
    curGlobalOffset += VarSize;
    Location *loc = new Location(gpRelative, curGlobalOffset-4, varName);
    return loc;
}

Location *CodeGenerator::GenParameter(int index, const char *varName)
{
    Location *loc = new Location(fpRelative, OffsetToFirstParam+index*VarSize, varName);
    return loc;
}

Location *CodeGenerator::GenIndirect(Location* base, int offset)
{
    Location *loc = new Location(base, offset);
    return loc;
}

Location *CodeGenerator::GenLoadConstant(int value)
{
    Location *result = GenTempVar();
    code->Append(new LoadConstant(result, value));
    return result;
}

Location *CodeGenerator::GenLoadConstant(const char *s)
{
    Location *result = GenTempVar();
    code->Append(new LoadStringConstant(result, s));
    return result;
}

Location *CodeGenerator::GenLoadLabel(const char *label)
{
    Location *result = GenTempVar();
    code->Append(new LoadLabel(result, label));
    return result;
}


void CodeGenerator::GenAssign(Location *dst, Location *src)
{
    code->Append(new Assign(dst, src));
}


Location *CodeGenerator::GenLoad(Location *ref, int offset)
{
    Location *result = GenTempVar();
    code->Append(new Load(result, ref, offset));
    return result;
}

void CodeGenerator::GenStore(Location *dst,Location *src, int offset)
{
    code->Append(new Store(dst, src, offset));
}


Location *CodeGenerator::GenBinaryOp(const char *opName, Location *op1,
                                     Location *op2)
{
    Location *result = GenTempVar();
    code->Append(new BinaryOp(BinaryOp::OpCodeForName(opName), result, op1, op2));
    return result;
}


void CodeGenerator::GenLabel(const char *label)
{
    code->Append(new Label(label));
}

void CodeGenerator::GenIfZ(Location *test, const char *label)
{
    code->Append(new IfZ(test, label));
}

void CodeGenerator::GenGoto(const char *label)
{
    code->Append(new Goto(label));
}

void CodeGenerator::GenReturn(Location *val)
{
    code->Append(new Return(val));
}

BeginFunc *CodeGenerator::GenBeginFunc()
{
  BeginFunc *result = new BeginFunc;
  code->Append(result);
  insideFn = code->NumElements() - 1;
  curStackOffset = OffsetToFirstLocal;
  return result;
}

void CodeGenerator::GenEndFunc()
{
  code->Append(new EndFunc());
  BeginFunc *beginFunc = dynamic_cast<BeginFunc*>(code->Nth(insideFn));
  Assert(beginFunc != NULL);
  beginFunc->SetFrameSize(OffsetToFirstLocal-curStackOffset);
}


void CodeGenerator::GenPushParam(Location *param)
{
    code->Append(new PushParam(param));
}

void CodeGenerator::GenPopParams(int numBytesOfParams)
{
    Assert(numBytesOfParams >= 0 && numBytesOfParams % VarSize == 0); // sanity check
    if (numBytesOfParams > 0)
        code->Append(new PopParams(numBytesOfParams));
}

Location *CodeGenerator::GenLCall(const char *label, bool fnHasReturnValue)
{
    Location *result = fnHasReturnValue ? GenTempVar() : NULL;
    code->Append(new LCall(label, result));
    return result;
}

Location *CodeGenerator::GenFunctionCall(const char *fnLabel, List<Location*> *args, bool hasReturnValue)
{
    for (int i = args->NumElements()-1; i >= 0; i--) // push params right to left
        GenPushParam(args->Nth(i));
    Location *result = GenLCall(fnLabel, hasReturnValue);
    GenPopParams(args->NumElements()*VarSize);
    return result;
}

Location *CodeGenerator::GenACall(Location *fnAddr, bool fnHasReturnValue)
{
    Location *result = fnHasReturnValue ? GenTempVar() : NULL;
    code->Append(new ACall(fnAddr, result));
    return result;
}

Location *CodeGenerator::GenMethodCall(Location *rcvr,
                                       Location *meth, List<Location*> *args, bool fnHasReturnValue)
{
    for (int i = args->NumElements()-1; i >= 0; i--)
        GenPushParam(args->Nth(i));
    GenPushParam(rcvr);	// hidden "this" parameter
    Location *result= GenACall(meth, fnHasReturnValue);
    GenPopParams((args->NumElements()+1)*VarSize);
    return result;
}


static struct _builtin {
    const char *label;
    int numArgs;
    bool hasReturn;
} builtins[] =
{{"_Alloc", 1, true},
    {"_ReadLine", 0, true},
    {"_ReadInteger", 0, true},
    {"_StringEqual", 2, true},
    {"_PrintInt", 1, false},
    {"_PrintString", 1, false},
    {"_PrintBool", 1, false},
    {"_Halt", 0, false}};

Location *CodeGenerator::GenBuiltInCall(BuiltIn bn,Location *arg1, Location *arg2)
{
    Assert(bn >= 0 && bn < NumBuiltIns);
    struct _builtin *b = &builtins[bn];
    Location *result = NULL;

    if (b->hasReturn) result = GenTempVar();
    // verify appropriate number of non-NULL arguments given
    Assert((b->numArgs == 0 && !arg1 && !arg2)
           || (b->numArgs == 1 && arg1 && !arg2)
           || (b->numArgs == 2 && arg1 && arg2));
    if (arg2) code->Append(new PushParam(arg2));
    if (arg1) code->Append(new PushParam(arg1));
    code->Append(new LCall(b->label, result));
    GenPopParams(VarSize*b->numArgs);
    return result;
}


void CodeGenerator::GenVTable(const char *className, List<const char *> *methodLabels)
{
    code->Append(new VTable(className, methodLabels));
}


void CodeGenerator::DoFinalCodeGen()
{   
    Genlabels();
    GenSucc();
    LivenessAnalysis();
    GraphColoring();
    GenRegMap();
    if (IsDebugOn("tac")) { // if debug don't translate to mips, just print Tac
        for (int i = 0; i < code->NumElements(); i++)
            code->Nth(i)->Print();
    }  else {
        Mips mips;
        mips.EmitPreamble();
        for (int i = 0; i < code->NumElements(); i++)
            code->Nth(i)->Emit(&mips);
    }
}



Location *CodeGenerator::GenArrayLen(Location *array)
{
    return GenLoad(array, -4);
}

Location *CodeGenerator::GenNew(const char *vTableLabel, int instanceSize)
{
    Location *size = GenLoadConstant(instanceSize);
    Location *result = GenBuiltInCall(Alloc, size);
    Location *vt = GenLoadLabel(vTableLabel);
    GenStore(result, vt);
    return result;
}


Location *CodeGenerator::GenDynamicDispatch(Location *rcvr, int vtableOffset, List<Location*> *args, bool hasReturnValue)
{
    Location *vptr = GenLoad(rcvr); // load vptr
    Assert(vtableOffset >= 0);
    Location *m = GenLoad(vptr, vtableOffset*4);
    return GenMethodCall(rcvr, m, args, hasReturnValue);
}

// all variables (ints, bools, ptrs, arrays) are 4 bytes in for code generation
// so this simplifies the math for offsets
Location *CodeGenerator::GenSubscript(Location *array, Location *index)
{
    Location *zero = GenLoadConstant(0);
    Location *isNegative = GenBinaryOp("<", index, zero);
    Location *count = GenLoad(array, -4);
    Location *isWithinRange = GenBinaryOp("<", index, count);
    Location *pastEnd = GenBinaryOp("==", isWithinRange, zero);
    Location *outOfRange = GenBinaryOp("||", isNegative, pastEnd);
    const char *pastError = NewLabel();
    GenIfZ(outOfRange, pastError);
    GenHaltWithMessage(err_arr_out_of_bounds);
    GenLabel(pastError);
    Location *four = GenLoadConstant(VarSize);
    Location *offset = GenBinaryOp("*", four, index);
    Location *elem = GenBinaryOp("+", array, offset);
    return GenIndirect(elem, 0); 
}



Location *CodeGenerator::GenNewArray(Location *numElems)
{
    Location *one = GenLoadConstant(1);
    Location *isNonpositive = GenBinaryOp("<", numElems, one);
    const char *pastError = NewLabel();
    GenIfZ(isNonpositive, pastError);
    GenHaltWithMessage(err_arr_bad_size);
    GenLabel(pastError);

    // need (numElems+1)*VarSize total bytes (extra 1 is for length)
    Location *arraySize = GenLoadConstant(1);
    Location *num = GenBinaryOp("+", arraySize, numElems);
    Location *four = GenLoadConstant(VarSize);
    Location *bytes = GenBinaryOp("*", num, four);
    Location *result = GenBuiltInCall(Alloc, bytes);
    GenStore(result, numElems);
    return GenBinaryOp("+", result, four);
}

void CodeGenerator::GenHaltWithMessage(const char *message)
{
    Location *msg = GenLoadConstant(message);
    GenBuiltInCall(PrintString, msg);
    GenBuiltInCall(Halt, NULL);
}
