// MIT License. Core remesher copyright (c) 2020 Jeremy HU; see LICENSE.
#ifndef AUTO_REMESHER_HDK_SOP_AUTO_REMESHER_H
#define AUTO_REMESHER_HDK_SOP_AUTO_REMESHER_H

#include <SOP/SOP_Node.h>

class UT_Interrupt;

namespace AutoRemesherHDK {

class SOP_AutoRemesher : public SOP_Node {
public:
    static OP_Node* myConstructor(OP_Network* net, const char* name, OP_Operator* op);
    static PRM_Template myTemplateList[];

protected:
    SOP_AutoRemesher(OP_Network* net, const char* name, OP_Operator* op);
    ~SOP_AutoRemesher() override;

    OP_ERROR cookMySop(OP_Context& context) override;
    const char* inputLabel(OP_InputIdx idx) const override;

private:
    int targetQuads(fpreal t) const { return evalInt("targetquads", 0, t); }
    fpreal edgeScaling(fpreal t) const { return evalFloat("edgescaling", 0, t); }
    fpreal adaptivity(fpreal t) const { return evalFloat("adaptivity", 0, t); }
    fpreal anisotropy(fpreal t) const { return evalFloat("anisotropy", 0, t); }
    fpreal sharpEdgeAngle(fpreal t) const { return evalFloat("sharpedgeangle", 0, t); }
    fpreal smoothNormalAngle(fpreal t) const { return evalFloat("smoothnormalangle", 0, t); }
    fpreal maxAspect(fpreal t) const { return evalFloat("maxaspect", 0, t); }
    fpreal adaptMin(fpreal t) const { return evalFloat("adaptmin", 0, t); }
    fpreal adaptMax(fpreal t) const { return evalFloat("adaptmax", 0, t); }
    bool printStats(fpreal t) const { return evalInt("printstats", 0, t) != 0; }

    static void progressTrampoline(void* tag, float progress, const char* status);
    void onProgress(float progress, const char* status);

    UT_Interrupt* myInterrupt = nullptr;
    bool myWasInterrupted = false;
};

} // namespace AutoRemesherHDK

#endif
