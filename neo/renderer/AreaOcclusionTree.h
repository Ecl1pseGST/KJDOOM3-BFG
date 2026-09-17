/*
===========================================================================
KJDOOM3-BFG — AreaOcclusionTree

Loose octree scoped to a single portalArea_t. Two independent uses:

  1. STATIC build, once at map load, over the area's world-model surfaces.
     Used to submit occluders into the masked occlusion buffer in roughly
     front-to-back order instead of raw dmap surface order (see
     tr_frontend_masked_occlusion_culling.cpp's current unsorted submission
     loop in R_FillMaskedOcclusionBufferWithModels).

  2. DYNAMIC insert/remove/update over areaReference_t entries. Used to
     replace the flat linked-list walk in AddAreaViewEntities() for areas
     that cross the activation threshold (see ShouldUseOcclusionTree() in
     RenderWorld_occlusion.cpp). Small/interior areas never build one of
     these at all — see INTEGRATION_NOTES.md.

Design constraints, deliberately:
  - This is NOT a general-purpose scene BVH. It is per-area, matches the
    lifetime of a portalArea_t, and is torn down/rebuilt with the area.
  - No dynamic re-splitting at runtime. Depth and leaf capacity are fixed
    at Init() time from the area's bounds; UpdateEntity() re-inserts into
    the existing tree shape rather than triggering a rebuild. If that
    proves too coarse for very active areas we revisit, but for
    Seelow-scale battlefields (props + AI, not per-blade-of-grass
    destructible terrain) a static shape is the right first cut.
===========================================================================
*/

#ifndef __AREAOCCLUSIONTREE_H__
#define __AREAOCCLUSIONTREE_H__

class idRenderEntityLocal;
struct areaReference_t;
struct srfTriangles_t;
struct modelSurface_t;
class idMaterial;

enum occTreeItemType_t
{
	OCC_ITEM_ENTITY,	// dynamic: ref->entity->globalReferenceBounds tracked live
	OCC_ITEM_SURFACE	// static: world-model surface, bounds fixed at build time
};

struct occTreeNode_t;	// KJ: forward declaration — occTreeItem_t needs a pointer to this
// before the real definition further down this file

struct occTreeItem_t
{
	occTreeItemType_t	type;
	idBounds			worldBounds;		// cached; authoritative copy lives on the source object
	areaReference_t*	ref;				// valid when type == OCC_ITEM_ENTITY
	srfTriangles_t*		surf;				// valid when type == OCC_ITEM_SURFACE
	const idMaterial*	shader;				// valid when type == OCC_ITEM_SURFACE — needed to skip
	// non-opaque surfaces (glass, fences) the same way the original
	// per-surface loop does via shader->IsDrawn()/Coverage()
	occTreeNode_t*		owner;				// KJ: the leaf node whose items list this is linked into —
	// required for RemoveEntity() to actually unlink itself rather
	// than leave a dangling pointer in that leaf's list.
	occTreeItem_t*		next;				// intrusive singly-linked list, owned by the leaf node
};

struct occTreeNode_t
{
	idBounds			bounds;				// pre-expanded by r_occlusionTreeLooseness so items near a
	// boundary don't ping-pong between siblings on every UpdateEntity()
	occTreeNode_t*		children[8];		// all NULL on a leaf
	occTreeItem_t*		items;				// non-NULL only on leaves
	int					itemCount;
	int					depth;
};

// Return false to stop descending into this node's children — e.g. the caller already
// found the node's bounds fully occluded via a masked-occlusion query, so there's no
// point rasterizing/testing anything further down that branch this frame.
typedef bool ( *occTreeVisit_t )( const occTreeNode_t* node, void* userData );

class idAreaOcclusionTree
{
public:
	idAreaOcclusionTree();
	~idAreaOcclusionTree();

	// areaBounds should be portalArea_t::globalBounds — it's already computed for the
	// light grid, so this doesn't add a new bounds calculation to load time.
	void					Init( const idBounds& areaBounds, int maxDepth = 5, int leafCapacity = 16 );
	void					Shutdown();

	// --- static path, called once from RenderWorld_load.cpp after an opted-in area's
	//     world-model surfaces are parsed --- takes modelSurface_t* so the shader
	//     travels with the geometry (needed to skip non-opaque surfaces at submission time)
	void					BuildStaticFromWorldSurfaces( const modelSurface_t* const* surfs, int numSurfs );

	// --- dynamic path, mirror these calls with AddEntityRefToArea() /
	//     idRenderWorldLocal's ref-unlink code so the tree never drifts from
	//     whatever areaReference_t bookkeeping already exists ---
	void					InsertEntity( areaReference_t* ref );
	void					RemoveEntity( areaReference_t* ref );
	void					UpdateEntity( areaReference_t* ref );	// call on entity move

	// Visits nodes in roughly front-to-back order relative to origin, recursing into
	// children only while visit() keeps returning true. Same traversal serves both
	// occluder submission (static tree, origin == view origin) and occludee gathering
	// (dynamic tree).
	void					TraverseFrontToBack( const idVec3& origin, occTreeVisit_t visit, void* userData ) const;

	int						GetItemCount() const
	{
		return itemCount;
	}
	bool					IsBuilt() const
	{
		return root != NULL;
	}

private:
	occTreeNode_t* 			BuildRecursive( const idBounds& bounds, int depth );
	void					InsertItemRecursive( occTreeNode_t* node, occTreeItem_t* item );
	void					CollectOrderedChildren( const occTreeNode_t* node, const idVec3& origin, occTreeNode_t* outOrder[8] ) const;
	void					TraverseRecursive( const occTreeNode_t* node, const idVec3& origin, occTreeVisit_t visit, void* userData ) const;

	occTreeNode_t* 			root;
	int						maxDepth;
	int						leafCapacity;
	int						itemCount;

	idBlockAlloc<occTreeNode_t, 64>		nodeAllocator;
	idBlockAlloc<occTreeItem_t, 256>	itemAllocator;
};

#endif // __AREAOCCLUSIONTREE_H__
