// ==========================================================================
// CharacterMorph.cpp — Character/object vertex morphing (skeletal animation)
// ==========================================================================
// Extracted from Characters.cpp to reduce monolithic file size.
// These functions interpolate animation frames to produce morphed vertices.

#include "Hunt.h"
#include "Loaders/LoadValidate.h"

void CreateChMorphedModel(TCharacter *cptr)
{

	TAni *aptr = &cptr->pinfo->Animation[cptr->Phase];
	TAni *paptr = &cptr->pinfo->Animation[cptr->PrevPhase];

	int CurFrame, SplineD, PCurFrame = 0, PSplineD = 0;
	float scale = cptr->scale;

	CurFrame = CalculateMorphFrameFixed(aptr->FramesCount, cptr->FTime, aptr->AniTime);
	SplineD = CurFrame & 0xFF;
	CurFrame = (CurFrame >> 8);


	std::int32_t PMorph = (cptr->Phase != cptr->PrevPhase) && (cptr->PPMorphTime < PMORPHTIME) && (MORPHP);

	if (PMorph)
	{
		PCurFrame = CalculateMorphFrameFixed(paptr->FramesCount, cptr->PrevPFTime, paptr->AniTime);
		PSplineD = PCurFrame & 0xFF;
		PCurFrame = (PCurFrame >> 8);
	}



	if (!MORPHA)
	{
		SplineD = 0;
		PSplineD = 0;
	}

	float k1, k2, pk1, pk2, pmk1, pmk2;

	k2 = static_cast<float>((SplineD)) / 256.f;
	k1 = 1.0f - k2;
	k1 /= 8.f;
	k2 /= 8.f;

	if (PMorph)
	{
		pk2 = static_cast<float>((PSplineD)) / 256.f;
		pk1 = 1.0f - pk2;
		pk1 /= 8.f;
		pk2 /= 8.f;
		pmk1 = static_cast<float>(cptr->PPMorphTime) / PMORPHTIME;
		pmk2 = 1.f - pmk1;
	}

	int VCount = cptr->pinfo->mptr->VCount;
	short int* adptr = aptr->aniData.get() + CurFrame * VCount * 3;
	short int* padptr = paptr->aniData.get() + PCurFrame * VCount * 3;

	float sb = static_cast<float>(sin(cptr->beta)) * scale;
	float cb = static_cast<float>(cos(cptr->beta)) * scale;
	float sg = static_cast<float>(sin(cptr->gamma));
	float cg = static_cast<float>(cos(cptr->gamma));

	for (int v = 0; v < VCount; v++)
	{

		

		float x = *(adptr + v * 3 + 0) * k1 + *(adptr + (v + VCount) * 3 + 0) * k2;
		float y = *(adptr + v * 3 + 1) * k1 + *(adptr + (v + VCount) * 3 + 1) * k2;
		float z = -(*(adptr + v * 3 + 2) * k1 + *(adptr + (v + VCount) * 3 + 2) * k2);

		if (PMorph)
		{
			float px = *(padptr + v * 3 + 0) * pk1 + *(padptr + (v + VCount) * 3 + 0) * pk2;
			float py = *(padptr + v * 3 + 1) * pk1 + *(padptr + (v + VCount) * 3 + 1) * pk2;
			float pz = -(*(padptr + v * 3 + 2) * pk1 + *(padptr + (v + VCount) * 3 + 2) * pk2);

			x = x * pmk1 + px * pmk2;
			y = y * pmk1 + py * pmk2;
			z = z * pmk1 + pz * pmk2;
		}


		float zz = z;
		float xx = cg * x - sg * y;
		float yy = cg * y + sg * x;


		//float fi = (z / 400) * (cptr->bend / 1.5f);
		float fi;
		if (z > 0)
		{
			fi = z / 240.f;
			if (fi > 1.f) fi = 1.f;
		}
		else
		{
			fi = z / 380.f;
			if (fi < -1.f) fi = -1.f;
		}

		float fiMosa = fi * cptr->bdepth;
		fi *= cptr->bend;
		if (!DinoInfo[cptr->CType].dontBend) {

			float bendc = static_cast<float>(cos(fi));
			float bends = static_cast<float>(sin(fi));

			float bx;
			float bz;
			float by;

			bx = bendc * xx - bends * zz;
			bz = bendc * zz + bends * xx;
			zz = bz;
			xx = bx;

			//if (DinoInfo[cptr->CType].Aquatic) {	//Also hunter corpse when killed by aquatic creature
			float bendcmosa = static_cast<float>(cos(fiMosa));
			float bendsmosa = static_cast<float>(sin(fiMosa));
			bz = bendcmosa * zz + bendsmosa * yy;
			by = bendcmosa * yy + bendsmosa * zz;
			yy = by;
			zz = bz;
			//}

		}


		cptr->pinfo->mptr->gVertex[v].x = xx * scale;
		cptr->pinfo->mptr->gVertex[v].y = cb * yy - sb * zz;
		cptr->pinfo->mptr->gVertex[v].z = cb * zz + sb * yy;


	}
}


void CreateMorphedModelBetaGamma(TModel* mptr, TAni *aptr, int FTime, float scale, float beta, float gamma)
{

	int CurFrame, SplineD, PCurFrame = 0, PSplineD = 0;

	CurFrame = CalculateMorphFrameFixed(aptr->FramesCount, FTime, aptr->AniTime);
	SplineD = CurFrame & 0xFF;
	CurFrame = (CurFrame >> 8);

	if (!MORPHA)
	{
		SplineD = 0;
		PSplineD = 0;
	}

	float k1, k2, pk1, pk2, pmk1, pmk2;

	k2 = static_cast<float>((SplineD)) / 256.f;
	k1 = 1.0f - k2;
	k1 /= 8.f;
	k2 /= 8.f;


	int VCount = mptr->VCount;
	short int* adptr = aptr->aniData.get() + CurFrame * VCount * 3;

	float sb = static_cast<float>(sin(beta)) * scale;
	float cb = static_cast<float>(cos(beta)) * scale;
	float sg = static_cast<float>(sin(gamma));
	float cg = static_cast<float>(cos(gamma));

	for (int v = 0; v < VCount; v++)
	{



		float x = *(adptr + v * 3 + 0) * k1 + *(adptr + (v + VCount) * 3 + 0) * k2;
		float y = *(adptr + v * 3 + 1) * k1 + *(adptr + (v + VCount) * 3 + 1) * k2;
		float z = -(*(adptr + v * 3 + 2) * k1 + *(adptr + (v + VCount) * 3 + 2) * k2);


		float zz = z;
		float xx = cg * x - sg * y;
		float yy = cg * y + sg * x;


		//float fi = (z / 400) * (cptr->bend / 1.5f);
		float fi;
		if (z > 0)
		{
			fi = z / 240.f;
			if (fi > 1.f) fi = 1.f;
		}
		else
		{
			fi = z / 380.f;
			if (fi < -1.f) fi = -1.f;
		}

		mptr->gVertex[v].x = xx * scale;
		mptr->gVertex[v].y = cb * yy - sb * zz;
		mptr->gVertex[v].z = cb * zz + sb * yy;


	}
}


void CreateMorphedModel(TModel* mptr, TAni *aptr, int FTime, float scale)
{
	int CurFrame = CalculateMorphFrameFixed(aptr->FramesCount, FTime, aptr->AniTime);

	int SplineD = CurFrame & 0xFF;
	CurFrame = (CurFrame >> 8);

	float k2 = static_cast<float>((SplineD)) / 256.f;
	float k1 = 1.0f - k2;
	k1 *= scale / 8.f;
	k2 *= scale / 8.f;

	int VCount = mptr->VCount;
	short int* adptr = &(aptr->aniData[CurFrame*VCount * 3]);
	for (int v = 0; v < VCount; v++)
	{
		mptr->gVertex[v].x = *(adptr + v * 3 + 0) * k1 + *(adptr + (v + VCount) * 3 + 0) * k2;
		mptr->gVertex[v].y = *(adptr + v * 3 + 1) * k1 + *(adptr + (v + VCount) * 3 + 1) * k2;
		mptr->gVertex[v].z = -*(adptr + v * 3 + 2) * k1 - *(adptr + (v + VCount) * 3 + 2) * k2;
	}
}



void CreateMorphedObject(TModel* mptr, TVTL &vtl, int FTime)
{
	int CurFrame = CalculateMorphFrameFixed(vtl.FramesCount, FTime, vtl.AniTime);

	int SplineD = CurFrame & 0xFF;
	CurFrame = (CurFrame >> 8);

	float k2 = static_cast<float>((SplineD)) / 256.f;
	float k1 = 1.0f - k2;
	k1 /= 8.f;
	k2 /= 8.f;

	int VCount = mptr->VCount;
	short int* adptr = &(vtl.aniData[CurFrame*VCount * 3]);
	for (int v = 0; v < VCount; v++)
	{
		mptr->gVertex[v].x = *(adptr + v * 3 + 0) * k1 + *(adptr + (v + VCount) * 3 + 0) * k2;
		mptr->gVertex[v].y = *(adptr + v * 3 + 1) * k1 + *(adptr + (v + VCount) * 3 + 1) * k2;
		mptr->gVertex[v].z = -*(adptr + v * 3 + 2) * k1 - *(adptr + (v + VCount) * 3 + 2) * k2;
	}
}
