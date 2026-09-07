// MIT License. Core remesher copyright (c) 2020 Jeremy HU; see LICENSE.
#include "SOP_AutoRemesher.h"

#include <AutoRemesher/AutoRemesher>

#include <GEO/GEO_PrimPoly.h>
#include <GU/GU_Detail.h>
#include <OP/OP_AutoLockInputs.h>
#include <OP/OP_Operator.h>
#include <OP/OP_OperatorTable.h>
#include <PRM/PRM_Include.h>
#include <UT/UT_DSOVersion.h>
#include <UT/UT_Interrupt.h>
#include <UT/UT_WorkBuffer.h>

#include <cstdint>
#include <iostream>
#include <streambuf>
#include <unordered_map>
#include <vector>

using namespace AutoRemesherHDK;

namespace {

// Silences the core's std::cerr report, which raises the Houdini console on Windows.
class NullBuffer : public std::streambuf {
protected:
    int overflow(int c) override { return c; }
};

class ScopedCerrSilence {
public:
    explicit ScopedCerrSilence(bool silence)
        : mySaved(silence ? std::cerr.rdbuf(&myNull) : nullptr)
    {
    }
    ~ScopedCerrSilence()
    {
        if (mySaved)
            std::cerr.rdbuf(mySaved);
    }

private:
    NullBuffer myNull;
    std::streambuf* mySaved;
};

} // namespace

static PRM_Name sTargetQuads("targetquads", "Target Quads");
static PRM_Name sEdgeScaling("edgescaling", "Edge Scaling");
static PRM_Name sAdaptivity("adaptivity", "Gradient Adaptivity");
static PRM_Name sAnisotropy("anisotropy", "Anisotropy");
static PRM_Name sSharpEdgeAngle("sharpedgeangle", "Sharp Edge Angle");
static PRM_Name sSmoothNormalAngle("smoothnormalangle", "Smooth Normal Angle");
static PRM_Name sMaxAspect("maxaspect", "Max Quad Aspect");
static PRM_Name sAdaptMin("adaptmin", "Adaptivity Min Ratio");
static PRM_Name sAdaptMax("adaptmax", "Adaptivity Max Ratio");
static PRM_Name sPrintStats("printstats", "Print Solver Stats");

static PRM_Default sTargetQuadsDefault(5000);
static PRM_Default sOneDefault(1.0);
static PRM_Default sSharpEdgeDefault(90.0);
static PRM_Default sZeroDefault(0.0);

static PRM_Range sTargetQuadsRange(PRM_RANGE_RESTRICTED, 100, PRM_RANGE_UI, 200000);
static PRM_Range sScalingRange(PRM_RANGE_RESTRICTED, 0.01, PRM_RANGE_UI, 5.0);
static PRM_Range sAdaptivityRange(PRM_RANGE_RESTRICTED, 0.0, PRM_RANGE_RESTRICTED, 1.0);
static PRM_Range sAnisotropyRange(PRM_RANGE_RESTRICTED, 0.0, PRM_RANGE_UI, 5.0);
static PRM_Range sAngleRange(PRM_RANGE_RESTRICTED, 0.0, PRM_RANGE_RESTRICTED, 180.0);
static PRM_Default sMaxAspectDefault(2.3);
static PRM_Default sAdaptMinDefault(0.3);
static PRM_Default sAdaptMaxDefault(3.0);
static PRM_Range sMaxAspectRange(PRM_RANGE_RESTRICTED, 1.0, PRM_RANGE_UI, 10.0);
static PRM_Range sAdaptMinRange(PRM_RANGE_RESTRICTED, 0.01, PRM_RANGE_RESTRICTED, 1.0);
static PRM_Range sAdaptMaxRange(PRM_RANGE_RESTRICTED, 1.0, PRM_RANGE_UI, 30.0);

PRM_Template SOP_AutoRemesher::myTemplateList[] = {
    PRM_Template(PRM_INT_J, 1, &sTargetQuads, &sTargetQuadsDefault, 0, &sTargetQuadsRange),
    PRM_Template(PRM_FLT_J, 1, &sEdgeScaling, &sOneDefault, 0, &sScalingRange),
    PRM_Template(PRM_FLT_J, 1, &sAdaptivity, &sOneDefault, 0, &sAdaptivityRange),
    PRM_Template(PRM_FLT_J, 1, &sAnisotropy, &sOneDefault, 0, &sAnisotropyRange),
    PRM_Template(PRM_FLT_J, 1, &sSharpEdgeAngle, &sSharpEdgeDefault, 0, &sAngleRange),
    PRM_Template(PRM_FLT_J, 1, &sSmoothNormalAngle, &sZeroDefault, 0, &sAngleRange),
    PRM_Template(PRM_FLT_J, 1, &sMaxAspect, &sMaxAspectDefault, 0, &sMaxAspectRange),
    PRM_Template(PRM_FLT_J, 1, &sAdaptMin, &sAdaptMinDefault, 0, &sAdaptMinRange),
    PRM_Template(PRM_FLT_J, 1, &sAdaptMax, &sAdaptMaxDefault, 0, &sAdaptMaxRange),
    PRM_Template(PRM_TOGGLE, 1, &sPrintStats, PRMzeroDefaults),
    PRM_Template()
};

OP_Node* SOP_AutoRemesher::myConstructor(OP_Network* net, const char* name, OP_Operator* op)
{
    return new SOP_AutoRemesher(net, name, op);
}

SOP_AutoRemesher::SOP_AutoRemesher(OP_Network* net, const char* name, OP_Operator* op)
    : SOP_Node(net, name, op)
{
    mySopFlags.setManagesDataIDs(true);
}

SOP_AutoRemesher::~SOP_AutoRemesher() = default;

const char* SOP_AutoRemesher::inputLabel(OP_InputIdx) const
{
    return "Triangle Mesh to Remesh";
}

void SOP_AutoRemesher::progressTrampoline(void* tag, float progress, const char* status)
{
    static_cast<SOP_AutoRemesher*>(tag)->onProgress(progress, status);
}

void SOP_AutoRemesher::onProgress(float progress, const char* status)
{
    if (nullptr == myInterrupt)
        return;
    if (nullptr != status)
        myInterrupt->setLongOpText(status);
    // Record the interrupt; it is reported after the solve returns.
    if (0 != myInterrupt->opInterrupt((int)(progress * 100.0f)))
        myWasInterrupted = true;
}

OP_ERROR SOP_AutoRemesher::cookMySop(OP_Context& context)
{
    OP_AutoLockInputs inputs(this);
    if (inputs.lock(context) >= UT_ERROR_ABORT)
        return error();

    const fpreal t = context.getTime();

    duplicateSource(0, context);

    gdp->convex(3);

    const GA_Size pointCount = gdp->getNumPoints();
    if (pointCount <= 0) {
        addError(SOP_MESSAGE, "Input geometry has no points");
        return error();
    }

    std::vector<AutoRemesher::Vector3> vertices((size_t)pointCount);
    {
        GA_Offset ptoff;
        GA_FOR_ALL_PTOFF(gdp, ptoff)
        {
            const UT_Vector3 p = gdp->getPos3(ptoff);
            const GA_Index index = gdp->pointIndex(ptoff);
            vertices[(size_t)index] = AutoRemesher::Vector3(p.x(), p.y(), p.z());
        }
    }

    std::vector<std::vector<size_t>> triangles;
    triangles.reserve((size_t)gdp->getNumPrimitives());
    {
        const GEO_Primitive* prim;
        GA_FOR_ALL_PRIMITIVES(gdp, prim)
        {
            if (3 != prim->getVertexCount())
                continue;
            triangles.push_back({
                (size_t)gdp->pointIndex(prim->getPointOffset(0)),
                (size_t)gdp->pointIndex(prim->getPointOffset(1)),
                (size_t)gdp->pointIndex(prim->getPointOffset(2)),
            });
        }
    }

    if (triangles.empty()) {
        addError(SOP_MESSAGE, "Input geometry contains no triangles after convexing");
        return error();
    }

    // The core crashes on an edge shared by more than two faces; reject it here.
    {
        std::unordered_map<uint64_t, int> edgeUse;
        edgeUse.reserve(triangles.size() * 3);
        for (const std::vector<size_t>& tri : triangles) {
            for (size_t k = 0; k < 3; ++k) {
                const size_t a = tri[k];
                const size_t b = tri[(k + 1) % 3];
                const uint64_t key = a < b
                    ? ((uint64_t)a << 32) | (uint32_t)b
                    : ((uint64_t)b << 32) | (uint32_t)a;
                ++edgeUse[key];
            }
        }
        size_t nonManifold = 0;
        for (const std::pair<const uint64_t, int>& entry : edgeUse) {
            if (entry.second > 2)
                ++nonManifold;
        }
        if (nonManifold > 0) {
            UT_WorkBuffer msg;
            msg.sprintf("Input is non-manifold: %d edge(s) shared by more than two faces. "
                        "Repair it first (a VDB from Polygons / Convert VDB round trip works).",
                (int)nonManifold);
            addError(SOP_MESSAGE, msg.buffer());
            return error();
        }
    }

    AutoRemesher::AutoRemesher remesher(vertices, triangles);

    const int quads = targetQuads(t);
    if (quads > 0)
        remesher.setTargetTriangleCount((size_t)quads * 2);

    const fpreal scaling = edgeScaling(t);
    if (scaling > 0)
        remesher.setScaling(scaling);

    remesher.setGradientAdaptivity(adaptivity(t));
    remesher.setAnisotropy(anisotropy(t));
    remesher.setSharpEdgeDegrees(sharpEdgeAngle(t));
    remesher.setSmoothNormalDegrees(smoothNormalAngle(t));
    remesher.setMaxAspectRatio(maxAspect(t));
    remesher.setAdaptivityRange(adaptMin(t), adaptMax(t));

    myWasInterrupted = false;
    bool remeshed = false;
    {
        ScopedCerrSilence quiet(!printStats(t));
        UT_AutoInterrupt progress("Remeshing to quads");
        myInterrupt = UTgetInterrupt();
        remesher.setTag(this);
        remesher.setProgressHandler(progressTrampoline);
        remeshed = remesher.remesh();
        myInterrupt = nullptr;
    }

    if (!remeshed) {
        addError(SOP_MESSAGE, "Quad remeshing failed on this input");
        return error();
    }

    const std::vector<AutoRemesher::Vector3>& remeshedVertices = remesher.remeshedVertices();
    const std::vector<std::vector<size_t>>& remeshedQuads = remesher.remeshedQuads();

    if (remeshedVertices.empty() || remeshedQuads.empty()) {
        addError(SOP_MESSAGE, "Quad remeshing produced empty geometry");
        return error();
    }

    gdp->clearAndDestroy();

    const GA_Offset firstPoint = gdp->appendPointBlock((GA_Size)remeshedVertices.size());
    for (size_t i = 0; i < remeshedVertices.size(); ++i) {
        const AutoRemesher::Vector3& v = remeshedVertices[i];
        gdp->setPos3(firstPoint + (GA_Offset)i,
            UT_Vector3((float)v.x(), (float)v.y(), (float)v.z()));
    }

    for (const std::vector<size_t>& face : remeshedQuads) {
        if (face.size() < 3)
            continue;
        GEO_PrimPoly* poly = GEO_PrimPoly::build(gdp, (GA_Size)face.size(), false, false);
        for (size_t k = 0; k < face.size(); ++k)
            poly->setPointOffset((GA_Size)k, firstPoint + (GA_Offset)face[k]);
    }

    if (myWasInterrupted)
        addWarning(SOP_MESSAGE, "Interrupt requested; the solve ran to completion");

    gdp->bumpDataIdsForAddOrRemove(true, true, true);

    return error();
}

void newSopOperator(OP_OperatorTable* table)
{
    OP_Operator* op = new OP_Operator(
        "autoremesher",
        "AutoRemesher",
        SOP_AutoRemesher::myConstructor,
        SOP_AutoRemesher::myTemplateList,
        1, // min inputs
        1, // max inputs
        nullptr,
        0);
    op->setIconName("SOP_quadremesh");
    table->addOperator(op);
}
