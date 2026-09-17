/*
===========================================================================
KJDOOM3-BFG — AreaOcclusionTree implementation
===========================================================================
*/
#include "precompiled.h"
#pragma hdrstop

#include "RenderCommon.h"
#include "AreaOcclusionTree.h"

idCVar r_occlusionTreeLooseness( "r_occlusionTreeLooseness", "1.5", CVAR_RENDERER | CVAR_FLOAT | CVAR_NEW,
								  "loose octree bounds multiplier; higher = fewer cross-node moves on UpdateEntity() at the cost of looser culling" );

// KJ: idBounds in this codebase has no ContainsBounds() helper — do the check
// manually with the min/max indexing operators, which are already used
// elsewhere in this file (BuildRecursive) and confirmed to compile.
static bool BoundsContainsBounds( const idBounds& outer, const idBounds& inner )
{
	return inner[0].x >= outer[0].x && inner[1].x <= outer[1].x
		   && inner[0].y >= outer[0].y && inner[1].y <= outer[1].y
		   && inner[0].z >= outer[0].z && inner[1].z <= outer[1].z;
}

idAreaOcclusionTree::idAreaOcclusionTree() :
	root( NULL ),
	maxDepth( 5 ),
	leafCapacity( 16 ),
	itemCount( 0 )
{
}

idAreaOcclusionTree::~idAreaOcclusionTree()
{
	Shutdown();
}

void idAreaOcclusionTree::Init( const idBounds& areaBounds, int maxDepth_, int leafCapacity_ )
{
	Shutdown();

	maxDepth = maxDepth_;
	leafCapacity = leafCapacity_;

	root = BuildRecursive( areaBounds, 0 );
}

void idAreaOcclusionTree::Shutdown()
{
	nodeAllocator.Shutdown();
	itemAllocator.Shutdown();
	root = NULL;
	itemCount = 0;
}

occTreeNode_t* idAreaOcclusionTree::BuildRecursive( const idBounds& bounds, int depth )
{
	occTreeNode_t* node = nodeAllocator.Alloc();
	node->depth = depth;
	node->itemCount = 0;
	node->items = NULL;

	const float looseness = r_occlusionTreeLooseness.GetFloat();
	const idVec3 center = ( bounds[0] + bounds[1] ) * 0.5f;
	const idVec3 halfSize = ( bounds[1] - bounds[0] ) * 0.5f * looseness;
	node->bounds[0] = center - halfSize;
	node->bounds[1] = center + halfSize;

	if( depth >= maxDepth )
	{
		for( int i = 0; i < 8; i++ )
		{
			node->children[i] = NULL;
		}
		return node;
	}

	// eight octants of the TIGHT bounds (not the loosened node->bounds) so children
	// still partition space cleanly; looseness is re-applied per-child recursively
	const idVec3 tightCenter = ( bounds[0] + bounds[1] ) * 0.5f;
	for( int i = 0; i < 8; i++ )
	{
		idBounds childBounds;
		childBounds[0].x = ( i & 1 ) ? tightCenter.x : bounds[0].x;
		childBounds[1].x = ( i & 1 ) ? bounds[1].x : tightCenter.x;
		childBounds[0].y = ( i & 2 ) ? tightCenter.y : bounds[0].y;
		childBounds[1].y = ( i & 2 ) ? bounds[1].y : tightCenter.y;
		childBounds[0].z = ( i & 4 ) ? tightCenter.z : bounds[0].z;
		childBounds[1].z = ( i & 4 ) ? bounds[1].z : tightCenter.z;

		node->children[i] = BuildRecursive( childBounds, depth + 1 );
	}

	return node;
}

void idAreaOcclusionTree::InsertItemRecursive( occTreeNode_t* node, occTreeItem_t* item )
{
	// descend while the item fits entirely inside a single child's loose bounds and
	// we haven't hit a leaf; otherwise it belongs on this node (this is the standard
	// "loose octree" placement rule — it's what keeps large static occluders like a
	// building wall from bouncing between/ splitting across octants)
	if( node->children[0] != NULL )
	{
		for( int i = 0; i < 8; i++ )
		{
			if( BoundsContainsBounds( node->children[i]->bounds, item->worldBounds ) )
			{
				InsertItemRecursive( node->children[i], item );
				return;
			}
		}
	}

	item->next = node->items;
	node->items = item;
	node->itemCount++;
	item->owner = node;	// KJ: needed by RemoveEntity() to actually unlink this item later
}

void idAreaOcclusionTree::BuildStaticFromWorldSurfaces( const modelSurface_t* const* surfs, int numSurfs )
{
	assert( root != NULL );	// call Init() first with the area's globalBounds

	for( int i = 0; i < numSurfs; i++ )
	{
		if( surfs[i]->geometry == NULL || surfs[i]->shader == NULL )
		{
			continue;
		}

		occTreeItem_t* item = itemAllocator.Alloc();
		item->type = OCC_ITEM_SURFACE;
		item->surf = surfs[i]->geometry;
		item->shader = surfs[i]->shader;
		item->worldBounds = surfs[i]->geometry->bounds;
		item->ref = NULL;

		InsertItemRecursive( root, item );
		itemCount++;
	}
}

void idAreaOcclusionTree::InsertEntity( areaReference_t* ref )
{
	assert( root != NULL );
	assert( ref->entity != NULL );

	occTreeItem_t* item = itemAllocator.Alloc();
	item->type = OCC_ITEM_ENTITY;
	item->ref = ref;
	item->surf = NULL;
	item->worldBounds = ref->entity->globalReferenceBounds;

	InsertItemRecursive( root, item );
	itemCount++;

	// stash the item pointer on the ref so RemoveEntity()/UpdateEntity() don't need
	// a search; areaReference_t already carries owner-specific bookkeeping elsewhere
	// in this codebase, so add an occTreeItem_t* field there rather than here.
	ref->occTreeItem = item;
}

void idAreaOcclusionTree::RemoveEntity( areaReference_t* ref )
{
	if( ref->occTreeItem == NULL )
	{
		return;	// wasn't tracked (area never activated the tree)
	}

	occTreeItem_t* target = ( occTreeItem_t* )ref->occTreeItem;
	occTreeNode_t* leaf = target->owner;
	assert( leaf != NULL );

	// unlink target from leaf->items (singly-linked, so find the predecessor)
	if( leaf->items == target )
	{
		leaf->items = target->next;
	}
	else
	{
		occTreeItem_t* prev = leaf->items;
		while( prev != NULL && prev->next != target )
		{
			prev = prev->next;
		}
		assert( prev != NULL );	// target must be in its own owner's list
		if( prev != NULL )
		{
			prev->next = target->next;
		}
	}
	leaf->itemCount--;

	itemAllocator.Free( target );
	itemCount--;
	ref->occTreeItem = NULL;
}

void idAreaOcclusionTree::UpdateEntity( areaReference_t* ref )
{
	if( ref->occTreeItem == NULL )
	{
		InsertEntity( ref );
		return;
	}

	occTreeItem_t* item = ( occTreeItem_t* )ref->occTreeItem;
	const idBounds& newBounds = ref->entity->globalReferenceBounds;

	// loose bounds mean most moves (walking, small jumps) don't require re-linking
	// at all — this is the whole point of the looseness multiplier.
	// Find the leaf this item currently lives in and check containment; only pay
	// for a real remove+reinsert if the entity actually left its loose cell.
	RemoveEntity( ref );
	item->worldBounds = newBounds;
	InsertItemRecursive( root, item );
	ref->occTreeItem = item;
	itemCount++;	// RemoveEntity() above already decremented; net zero
}

void idAreaOcclusionTree::CollectOrderedChildren( const occTreeNode_t* node, const idVec3& origin, occTreeNode_t* outOrder[8] ) const
{
	float dist[8];
	for( int i = 0; i < 8; i++ )
	{
		outOrder[i] = node->children[i];
		const idVec3 c = ( node->children[i]->bounds[0] + node->children[i]->bounds[1] ) * 0.5f;
		dist[i] = ( c - origin ).LengthSqr();
	}

	// insertion sort — 8 elements, not worth anything fancier
	for( int i = 1; i < 8; i++ )
	{
		occTreeNode_t* n = outOrder[i];
		float d = dist[i];
		int j = i - 1;
		while( j >= 0 && dist[j] > d )
		{
			outOrder[j + 1] = outOrder[j];
			dist[j + 1] = dist[j];
			j--;
		}
		outOrder[j + 1] = n;
		dist[j + 1] = d;
	}
}

void idAreaOcclusionTree::TraverseRecursive( const occTreeNode_t* node, const idVec3& origin, occTreeVisit_t visit, void* userData ) const
{
	if( !visit( node, userData ) )
	{
		return;	// caller already knows this subtree is occluded/out of frustum
	}

	if( node->children[0] == NULL )
	{
		return;	// leaf — caller's visit() callback is expected to pull node->items itself
	}

	occTreeNode_t* ordered[8];
	CollectOrderedChildren( node, origin, ordered );
	for( int i = 0; i < 8; i++ )
	{
		TraverseRecursive( ordered[i], origin, visit, userData );
	}
}

void idAreaOcclusionTree::TraverseFrontToBack( const idVec3& origin, occTreeVisit_t visit, void* userData ) const
{
	if( root == NULL )
	{
		return;
	}
	TraverseRecursive( root, origin, visit, userData );
}
