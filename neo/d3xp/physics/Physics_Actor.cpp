/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "precompiled.h"
#pragma hdrstop


#include "../Game_local.h"

CLASS_DECLARATION( idPhysics_Base, idPhysics_Actor )
END_CLASS

/*
================
idPhysics_Actor::idPhysics_Actor
================
*/
idPhysics_Actor::idPhysics_Actor()
{
	clipModel = NULL;
	SetClipModelAxis();
	mass = 100.0f;
	invMass = 1.0f / mass;
	masterEntity = NULL;
	masterYaw = 0.0f;
	masterDeltaYaw = 0.0f;
	groundEntityPtr = NULL;

	// RB: water level shared by all actors
	waterLevel = WATERLEVEL_NONE;
	waterType = 0;
	waterBodyPtr = NULL;
	// RB end
}

/*
================
idPhysics_Actor::~idPhysics_Actor
================
*/
idPhysics_Actor::~idPhysics_Actor()
{
	if( clipModel )
	{
		delete clipModel;
		clipModel = NULL;
	}
}

/*
================
idPhysics_Actor::Save
================
*/
void idPhysics_Actor::Save( idSaveGame* savefile ) const
{

	savefile->WriteClipModel( clipModel );
	savefile->WriteMat3( clipModelAxis );

	savefile->WriteFloat( mass );
	savefile->WriteFloat( invMass );

	savefile->WriteObject( masterEntity );
	savefile->WriteFloat( masterYaw );
	savefile->WriteFloat( masterDeltaYaw );

	groundEntityPtr.Save( savefile );

	// RB: water level shared by all actors
	savefile->WriteInt( ( int )waterLevel );
	savefile->WriteInt( waterType );
	waterBodyPtr.Save( savefile );
	// RB end
}

/*
================
idPhysics_Actor::Restore
================
*/
void idPhysics_Actor::Restore( idRestoreGame* savefile )
{

	savefile->ReadClipModel( clipModel );
	savefile->ReadMat3( clipModelAxis );

	savefile->ReadFloat( mass );
	savefile->ReadFloat( invMass );

	savefile->ReadObject( reinterpret_cast<idClass*&>( masterEntity ) );
	savefile->ReadFloat( masterYaw );
	savefile->ReadFloat( masterDeltaYaw );

	groundEntityPtr.Restore( savefile );

	// RB: water level shared by all actors
	savefile->ReadInt( ( int& )waterLevel );
	savefile->ReadInt( waterType );
	waterBodyPtr.Restore( savefile );
	// RB end
}

/*
================
idPhysics_Actor::SetClipModelAxis
================
*/
void idPhysics_Actor::SetClipModelAxis()
{
	// align clip model to gravity direction
	if( ( gravityNormal[2] == -1.0f ) || ( gravityNormal == vec3_zero ) )
	{
		clipModelAxis.Identity();
	}
	else
	{
		clipModelAxis[2] = -gravityNormal;
		clipModelAxis[2].NormalVectors( clipModelAxis[0], clipModelAxis[1] );
		clipModelAxis[1] = -clipModelAxis[1];
	}

	if( clipModel )
	{
		clipModel->Link( gameLocal.clip, self, 0, clipModel->GetOrigin(), clipModelAxis );
	}
}

/*
================
idPhysics_Actor::GetGravityAxis
================
*/
const idMat3& idPhysics_Actor::GetGravityAxis() const
{
	return clipModelAxis;
}

/*
================
idPhysics_Actor::GetMasterDeltaYaw
================
*/
float idPhysics_Actor::GetMasterDeltaYaw() const
{
	return masterDeltaYaw;
}

/*
================
idPhysics_Actor::GetGroundEntity
================
*/
idEntity* idPhysics_Actor::GetGroundEntity() const
{
	return groundEntityPtr.GetEntity();
}

/*
================
idPhysics_Actor::SetClipModel
================
*/
void idPhysics_Actor::SetClipModel( idClipModel* model, const float density, int id, bool freeOld )
{
	assert( self );
	assert( model );					// a clip model is required
	assert( model->IsTraceModel() );	// and it should be a trace model
	assert( density > 0.0f );			// density should be valid

	if( clipModel && clipModel != model && freeOld )
	{
		delete clipModel;
	}
	clipModel = model;
	clipModel->Link( gameLocal.clip, self, 0, clipModel->GetOrigin(), clipModelAxis );
}

/*
================
idPhysics_Actor::GetClipModel
================
*/
idClipModel* idPhysics_Actor::GetClipModel( int id ) const
{
	return clipModel;
}

/*
================
idPhysics_Actor::GetNumClipModels
================
*/
int idPhysics_Actor::GetNumClipModels() const
{
	return 1;
}

/*
================
idPhysics_Actor::SetMass
================
*/
void idPhysics_Actor::SetMass( float _mass, int id )
{
	assert( _mass > 0.0f );
	mass = _mass;
	invMass = 1.0f / _mass;
}

/*
================
idPhysics_Actor::GetMass
================
*/
float idPhysics_Actor::GetMass( int id ) const
{
	return mass;
}

/*
================
idPhysics_Actor::SetClipMask
================
*/
void idPhysics_Actor::SetContents( int contents, int id )
{
	clipModel->SetContents( contents );
}

/*
================
idPhysics_Actor::SetClipMask
================
*/
int idPhysics_Actor::GetContents( int id ) const
{
	return clipModel->GetContents();
}

/*
================
idPhysics_Actor::GetBounds
================
*/
const idBounds& idPhysics_Actor::GetBounds( int id ) const
{
	return clipModel->GetBounds();
}

/*
================
idPhysics_Actor::GetAbsBounds
================
*/
const idBounds& idPhysics_Actor::GetAbsBounds( int id ) const
{
	return clipModel->GetAbsBounds();
}

/*
================
idPhysics_Actor::IsPushable
================
*/
bool idPhysics_Actor::IsPushable() const
{
	return ( masterEntity == NULL );
}

/*
================
idPhysics_Actor::GetOrigin
================
*/
const idVec3& idPhysics_Actor::GetOrigin( int id ) const
{
	return clipModel->GetOrigin();
}

/*
================
idPhysics_Player::GetAxis
================
*/
const idMat3& idPhysics_Actor::GetAxis( int id ) const
{
	return clipModel->GetAxis();
}

/*
================
idPhysics_Actor::SetGravity
================
*/
void idPhysics_Actor::SetGravity( const idVec3& newGravity )
{
	if( newGravity != gravityVector )
	{
		idPhysics_Base::SetGravity( newGravity );
		SetClipModelAxis();
	}
}

/*
================
idPhysics_Actor::ClipTranslation
================
*/
void idPhysics_Actor::ClipTranslation( trace_t& results, const idVec3& translation, const idClipModel* model ) const
{
	if( model )
	{
		gameLocal.clip.TranslationModel( results, clipModel->GetOrigin(), clipModel->GetOrigin() + translation,
										 clipModel, clipModel->GetAxis(), clipMask,
										 model->Handle(), model->GetOrigin(), model->GetAxis() );
	}
	else
	{
		gameLocal.clip.Translation( results, clipModel->GetOrigin(), clipModel->GetOrigin() + translation,
									clipModel, clipModel->GetAxis(), clipMask, self );
	}
}

/*
================
idPhysics_Actor::ClipRotation
================
*/
void idPhysics_Actor::ClipRotation( trace_t& results, const idRotation& rotation, const idClipModel* model ) const
{
	if( model )
	{
		gameLocal.clip.RotationModel( results, clipModel->GetOrigin(), rotation,
									  clipModel, clipModel->GetAxis(), clipMask,
									  model->Handle(), model->GetOrigin(), model->GetAxis() );
	}
	else
	{
		gameLocal.clip.Rotation( results, clipModel->GetOrigin(), rotation,
								 clipModel, clipModel->GetAxis(), clipMask, self );
	}
}

/*
================
idPhysics_Actor::ClipContents
================
*/
int idPhysics_Actor::ClipContents( const idClipModel* model ) const
{
	if( model )
	{
		return gameLocal.clip.ContentsModel( clipModel->GetOrigin(), clipModel, clipModel->GetAxis(), -1,
											 model->Handle(), model->GetOrigin(), model->GetAxis() );
	}
	else
	{
		return gameLocal.clip.Contents( clipModel->GetOrigin(), clipModel, clipModel->GetAxis(), -1, NULL );
	}
}

/*
================
idPhysics_Actor::DisableClip
================
*/
void idPhysics_Actor::DisableClip()
{
	clipModel->Disable();
}

/*
================
idPhysics_Actor::EnableClip
================
*/
void idPhysics_Actor::EnableClip()
{
	clipModel->Enable();
}

/*
================
idPhysics_Actor::UnlinkClip
================
*/
void idPhysics_Actor::UnlinkClip()
{
	clipModel->Unlink();
}

/*
================
idPhysics_Actor::LinkClip
================
*/
void idPhysics_Actor::LinkClip()
{
	clipModel->Link( gameLocal.clip, self, 0, clipModel->GetOrigin(), clipModel->GetAxis() );
}

/*
================
idPhysics_Actor::EvaluateContacts
================
*/
bool idPhysics_Actor::EvaluateContacts()
{

	// get all the ground contacts
	ClearContacts();
	AddGroundContacts( clipModel );
	AddContactEntitiesForContacts();

	return ( contacts.Num() != 0 );
}

/*
================
idPhysics_Actor::GetWaterLevel

RB: shared by all actors (players, monsters). Mirrors the check that used to
live only in idPhysics_Player::SetWaterLevel, but also resolves which specific
liquid entity (if any) is touched, so gameplay code (splash sounds, per-body
buoyancy/current, etc.) can react to a particular body of water rather than
just a generic content flag. Notifies self via idEntity::EnterLiquid/ExitLiquid
on state transitions.
================
*/
void idPhysics_Actor::SetWaterLevel()
{
	idVec3		point;
	idBounds	bounds;
	int			contents;

	waterLevel_t oldWaterLevel = waterLevel;
	idEntity* oldWaterBody = waterBodyPtr.GetEntity();

	waterLevel = WATERLEVEL_NONE;
	waterType = 0;

	if( !clipModel )
	{
		return;
	}

	bounds = clipModel->GetBounds();
	idVec3 origin = clipModel->GetOrigin();

	// check at feet level
	point = origin - ( bounds[0][2] + 1.0f ) * gravityNormal;
	contents = gameLocal.clip.Contents( point, NULL, mat3_identity, -1, self );
	if( contents & MASK_LIQUID )
	{
		waterType = contents;
		waterLevel = WATERLEVEL_FEET;

		// check at waist level
		point = origin - ( bounds[1][2] - bounds[0][2] ) * 0.5f * gravityNormal;
		contents = gameLocal.clip.Contents( point, NULL, mat3_identity, -1, self );
		if( contents & MASK_LIQUID )
		{
			waterLevel = WATERLEVEL_WAIST;

			// check at head level
			point = origin - ( bounds[1][2] - 1.0f ) * gravityNormal;
			contents = gameLocal.clip.Contents( point, NULL, mat3_identity, -1, self );
			if( contents & MASK_LIQUID )
			{
				waterLevel = WATERLEVEL_HEAD;
			}
		}
	}

	// resolve which specific liquid entity (if any) is touched at the deepest
	// currently-valid check point, so EnterLiquid/ExitLiquid get a real entity
	// pointer to work with instead of just a content bitmask
	idEntity* newWaterBody = NULL;
	if( waterLevel != WATERLEVEL_NONE )
	{
		idBounds pointBounds( point - idVec3( 4.0f, 4.0f, 4.0f ), point + idVec3( 4.0f, 4.0f, 4.0f ) );
		idEntity* touchList[16];
		int numTouching = gameLocal.clip.EntitiesTouchingBounds( pointBounds, MASK_LIQUID, touchList, 16 );
		for( int i = 0; i < numTouching; i++ )
		{
			if( touchList[i] != self )
			{
				newWaterBody = touchList[i];
				break;
			}
		}
	}
	waterBodyPtr = newWaterBody;

	// notify the owning entity of enter/exit transitions
	if( self )
	{
		bool wasInWater = ( oldWaterLevel != WATERLEVEL_NONE );
		bool isInWater = ( waterLevel != WATERLEVEL_NONE );

		if( isInWater && ( !wasInWater || newWaterBody != oldWaterBody ) )
		{
			self->EnterLiquid( newWaterBody );
		}
		else if( !isInWater && wasInWater )
		{
			self->ExitLiquid( oldWaterBody );
		}
	}
}

/*
================
idPhysics_Actor::GetWaterLevel
================
*/
waterLevel_t idPhysics_Actor::GetWaterLevel() const
{
	return waterLevel;
}

/*
================
idPhysics_Actor::GetWaterType
================
*/
int idPhysics_Actor::GetWaterType() const
{
	return waterType;
}

/*
================
idPhysics_Actor::GetWaterBody
================
*/
idEntity* idPhysics_Actor::GetWaterBody() const
{
	return waterBodyPtr.GetEntity();
}
// RB end
