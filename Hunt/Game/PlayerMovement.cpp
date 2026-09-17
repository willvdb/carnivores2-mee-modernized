// ==========================================================================
// PlayerMovement.cpp
// ==========================================================================

#include "Hunt.h"
#include "Platform/Platform.h"

void ProcessReload() {

	TWeapon *wptr = &Weapon;

	if (wptr->state == 2 && wptr->FTime == 0)
		if (WeapInfo[CurrentWeapon].Reload) {
			if (Chambered[CurrentWeapon] < WeapInfo[CurrentWeapon].Reload &&
				ShotsLeft[CurrentWeapon]) {

				wptr->ammoIn = WeapInfo[CurrentWeapon].Reload - Chambered[CurrentWeapon];
				if (ShotsLeft[CurrentWeapon] < WeapInfo[CurrentWeapon].Reload) wptr->ammoIn = ShotsLeft[CurrentWeapon];

				if ((Chambered[CurrentWeapon] || ShotsLeft[CurrentWeapon] < WeapInfo[CurrentWeapon].Reload)
					&& WeapInfo[CurrentWeapon].rldAnimPart >= 0) {

					//state 5
					if (wptr->chinfo[CurrentWeapon].Animation[WeapInfo[CurrentWeapon].rldAnimPart].AniTime)
					{
						wptr->state = 5;
						wptr->FTime = 1;
						if (IsUnderwater()) {
							if (WeapInfo[CurrentWeapon].rldAqSndPart >= 0)
								AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].rldAqSndPart].length,
									wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].rldAqSndPart].lpData.data(), 256);
						}
						else {
							int fx = wptr->chinfo[CurrentWeapon].Anifx[WeapInfo[CurrentWeapon].rldAnimPart];
							if (fx >= 0) AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[fx].length,
								wptr->chinfo[CurrentWeapon].SoundFX[fx].lpData.data(), 256);
						}
					}

				}
				else {

					//state 4
					if (wptr->chinfo[CurrentWeapon].Animation[WeapInfo[CurrentWeapon].rldAnim].AniTime)
					{
						wptr->state = 4;
						wptr->FTime = 1;
						if (IsUnderwater()) {
							if (WeapInfo[CurrentWeapon].rldAqSnd >= 0)
								AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].rldAqSnd].length,
									wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].rldAqSnd].lpData.data(), 256);
						}
						else {
							int fx = wptr->chinfo[CurrentWeapon].Anifx[WeapInfo[CurrentWeapon].rldAnim];
							if (fx >= 0) AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[fx].length,
								wptr->chinfo[CurrentWeapon].SoundFX[fx].lpData.data(), 256);
						}
					}

				}


			}

		}
		else if (AmmoMag[CurrentWeapon]) {

			/*
			int temp = MagShotsLeft[CurrentWeapon];
			MagShotsLeft[CurrentWeapon] = ShotsLeft[CurrentWeapon];
			ShotsLeft[CurrentWeapon] = temp;

			if (!MagShotsLeft[CurrentWeapon]) AmmoMag[CurrentWeapon]--;

			if (!Chambered[CurrentWeapon]) {
				Chambered[CurrentWeapon] = 1;
				ShotsLeft[CurrentWeapon]--;
			}
			*/

			if (Chambered[CurrentWeapon] && WeapInfo[CurrentWeapon].rldAnimPart >= 0) {


				if (wptr->chinfo[CurrentWeapon].Animation[WeapInfo[CurrentWeapon].rldAnimPart].AniTime)
				{

					wptr->state = 5;
					wptr->FTime = 1;
					if (IsUnderwater()) {
						if (WeapInfo[CurrentWeapon].rldAqSndPart >= 0)
							AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].rldAqSndPart].length,
								wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].rldAqSndPart].lpData.data(), 256);
					}
					else {
						int fx = wptr->chinfo[CurrentWeapon].Anifx[WeapInfo[CurrentWeapon].rldAnimPart];
						if (fx >= 0) AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[fx].length,
							wptr->chinfo[CurrentWeapon].SoundFX[fx].lpData.data(), 256);
					}
				}
			}
			else {

				if (wptr->chinfo[CurrentWeapon].Animation[WeapInfo[CurrentWeapon].rldAnim].AniTime)
				{

					wptr->state = 4;
					wptr->FTime = 1;
					if (IsUnderwater()) {
						if (WeapInfo[CurrentWeapon].rldAqSnd >= 0)
							AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].rldAqSnd].length,
								wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].rldAqSnd].lpData.data(), 256);
					}
					else {
						int fx = wptr->chinfo[CurrentWeapon].Anifx[WeapInfo[CurrentWeapon].rldAnim];
						if (fx >= 0) AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[fx].length,
							wptr->chinfo[CurrentWeapon].SoundFX[fx].lpData.data(), 256);
					}

				}

			}




		}

}

void ProcessFireMode() {
	TWeapon *wptr = &Weapon;

	if (!WeapInfo[CurrentWeapon].semiauto) return;
	if (!WeapInfo[CurrentWeapon].fullauto) return;
	if (WeapInfo[CurrentWeapon].modAnim <= 0) return;

	if (wptr->state == 2 && wptr->FTime == 0) {
		wptr->state = 7;
		wptr->FTime = 1;
		if (IsUnderwater()) {
			if (WeapInfo[CurrentWeapon].modAqSnd >= 0)
				AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].modAqSnd].length,
					wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].modAqSnd].lpData.data(), 256);
		}
		else {
			int fx = wptr->chinfo[CurrentWeapon].Anifx[WeapInfo[CurrentWeapon].modAnim];
			if (fx >= 0) AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[fx].length,
				wptr->chinfo[CurrentWeapon].SoundFX[fx].lpData.data(), 256);
		}
	}

}

void ProcessPump() {
	TWeapon *wptr = &Weapon;

	if (WeapInfo[CurrentWeapon].pmpAnim <= 0) return;

	if (wptr->state == 2 && wptr->FTime == 0)
		if (!WeapInfo[CurrentWeapon].Reload) {
			Chambered[CurrentWeapon] = 0;
			//if (!ShotsLeft[CurrentWeapon]) return;
			wptr->state = 6;
			wptr->FTime = 1;
			if (IsUnderwater()) {
				if (WeapInfo[CurrentWeapon].pmpAqSnd >= 0)
					AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].pmpAqSnd].length,
						wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].pmpAqSnd].lpData.data(), 256);
			}
			else {
				int fx = wptr->chinfo[CurrentWeapon].Anifx[WeapInfo[CurrentWeapon].pmpAnim];
				if (fx >= 0) AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[fx].length,
					wptr->chinfo[CurrentWeapon].SoundFX[fx].lpData.data(), 256);
			}
		}
}

void ProcessShoot()
{
  //if (HeadBackR) return;
  // No firing in the trophy room: entering it never lowers a raised
  // weapon (HideWeapon early-returns in TrophyMode), so without this the
  // previously-raised gun stays live while walking the exhibits.
  // InTrophyRoomMap() backs the mode check: the mode has many writers,
  // the map name is set once from the command line.
  if (g_GameMode == GameMode::TrophyMode || InTrophyRoomMap()) return;
	
  TWeapon *wptr = &Weapon;
  if (IsUnderwater() && !WeapInfo[CurrentWeapon].harpoon)
  {
    HideWeapon();
    return;
  }

  if (wptr->state == 2 && wptr->FTime==0)
  {
	  int clickNo = rRand(2);
	  if (!Chambered[CurrentWeapon]) {
		  if (!alreadyFired && !IsUnderwater()) AddVoicev(fxClick[clickNo].length, fxClick[clickNo].lpData.data(), 256);
		  return;
	  }

	  if (alreadyFired && FiringMode[CurrentWeapon] == 0) return;

    wptr->FTime = 1;
    HeadBackR=64;
	Recoil.y = -static_cast<float>(WeapInfo[CurrentWeapon].recoil) / 100.f;
	float rx = rRand(8);
	rx -= 4;
	rx /= 8;
	rx *= static_cast<float>(WeapInfo[CurrentWeapon].recoil) / 100.f;
	Recoil.x += rx;

	if (IsUnderwater()) {
		if (WeapInfo[CurrentWeapon].shtAqSnd >= 0)
			AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].shtAqSnd].length,
				wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].shtAqSnd].lpData.data(), 256);
	}
	else {
		int fx = wptr->chinfo[CurrentWeapon].Anifx[WeapInfo[CurrentWeapon].shtAnim];
		if (fx >= 0) AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[fx].length,
			wptr->chinfo[CurrentWeapon].SoundFX[fx].lpData.data(), 256);
	}
	
	TrophyRoom.Last.smade++;

	if (WeapInfo[CurrentWeapon].MuzzFlash|| WeapInfo[CurrentWeapon].ChamFlash)wptr->FlashP = 1;

	if (WeapInfo[CurrentWeapon].MuzzFlash) {
		Muzz = true;
		MuzzGamma = rRand(100);
		MuzzGamma /= 50;
		MuzzGamma *= pi;
	}

    for (int s=0; s<=WeapInfo[CurrentWeapon].TraceC; s++)
    {
      float rA = 0;
      float rB = 0;

	  if (IsUnderwater()) {
		  rA = siRand(128) * 0.00010 * (2.f - WeapInfo[CurrentWeapon].PrecAq);
		  rB = siRand(128) * 0.00010 * (2.f - WeapInfo[CurrentWeapon].PrecAq);
	  }else{
		  rA = siRand(128) * 0.00010 * (2.f - WeapInfo[CurrentWeapon].Prec);
		  rB = siRand(128) * 0.00010 * (2.f - WeapInfo[CurrentWeapon].Prec);
	  }


      float ca = static_cast<float>(cos(PlayerAlpha + wpnDAlpha + rA));
      float sa = static_cast<float>(sin(PlayerAlpha + wpnDAlpha + rA));
      float cb = static_cast<float>(cos(PlayerBeta + wpnDBeta + rB));
      float sb = static_cast<float>(sin(PlayerBeta + wpnDBeta + rB));

      nv.x=sa;
      nv.y=0;
      nv.z=-ca;

      nv.x*=cb;
      nv.y=-sb;
      nv.z*=cb;

	  float v = WeapInfo[CurrentWeapon].Veloc;
	  if (IsUnderwater()) v = WeapInfo[CurrentWeapon].VelocAq;
	  float l = WeapInfo[CurrentWeapon].Veloc;
	  if (WeapInfo[CurrentWeapon].aqLow) l = WeapInfo[CurrentWeapon].VelocAq;

      AddBullet(PlayerX, PlayerY+HeadY, PlayerZ,
               nv.x * 64* v,
               nv.y * 64* v,
	           nv.z * 64* v,
		  nv.x * 64 * l,
		  nv.y * 64 * l,
		  nv.z * 64 * l,
			   CurrentWeapon,
			   false);
    }

	//Multiplayer)
	sendGunShot = CurrentWeapon;
	

    Vector3d v;
    v.x = PlayerX;
    v.y = PlayerY;
    v.z = PlayerZ;
    if (!IsUnderwater()) MakeNoise(v, ctViewR*200 * WeapInfo[CurrentWeapon].Loud);
    Chambered[CurrentWeapon]-=1;
//	else if (WeapInfo[CurrentWeapon].Reload) {
//		if (!Chambered[CurrentWeapon]) Chambered[CurrentWeapon] = WeapInfo[CurrentWeapon].Reload;
//	}
  }
}

void ProcessSlide()
{
  if (NOCLIP || IsUnderwater()) return;
  float ch = GetLandQHNoObj(PlayerX, PlayerZ);
  float mh = ch;
  float chh;
  int   sd = 0;

  chh=GetLandQHNoObj(PlayerX - 16, PlayerZ);
  if (chh<mh)
  {
    mh = chh;
    sd = 1;
  }
  chh=GetLandQHNoObj(PlayerX + 16, PlayerZ);
  if (chh<mh)
  {
    mh = chh;
    sd = 2;
  }
  chh=GetLandQHNoObj(PlayerX, PlayerZ - 16);
  if (chh<mh)
  {
    mh = chh;
    sd = 3;
  }
  chh=GetLandQHNoObj(PlayerX, PlayerZ + 16);
  if (chh<mh)
  {
    mh = chh;
    sd = 4;
  }

  chh=GetLandQHNoObj(PlayerX - 12, PlayerZ - 12);
  if (chh<mh)
  {
    mh = chh;
    sd = 5;
  }
  chh=GetLandQHNoObj(PlayerX + 12, PlayerZ - 12);
  if (chh<mh)
  {
    mh = chh;
    sd = 6;
  }
  chh=GetLandQHNoObj(PlayerX - 12, PlayerZ + 12);
  if (chh<mh)
  {
    mh = chh;
    sd = 7;
  }
  chh=GetLandQHNoObj(PlayerX + 12, PlayerZ + 12);
  if (chh<mh)
  {
    mh = chh;
    sd = 8;
  }

  if (!NOCLIP)
    if (mh<ch-16)
    {
      float delta = (ch-mh) / 4;
      if (sd == 1)
      {
        PlayerX -= delta;
      }
      if (sd == 2)
      {
        PlayerX += delta;
      }
      if (sd == 3)
      {
        PlayerZ -= delta;
      }
      if (sd == 4)
      {
        PlayerZ += delta;
      }

      delta*=0.7f;
      if (sd == 5)
      {
        PlayerX -= delta;
        PlayerZ -= delta;
      }
      if (sd == 6)
      {
        PlayerX += delta;
        PlayerZ -= delta;
      }
      if (sd == 7)
      {
        PlayerX -= delta;
        PlayerZ += delta;
      }
      if (sd == 8)
      {
        PlayerX += delta;
        PlayerZ += delta;
      }
    }
}

void ProcessPlayerMovement()
{

  Platform::Point ms = Platform::PointerInClient();
  if (REVERSEMS) ms.y = -ms.y+VideoCY*2;
  // The per-frame mouse delta naturally scales with frame time because the
  // cursor is reset to the centre every frame, so ms-VideoCX/Y ~= V*T. The
  // original `rav += D * K` is therefore already framerate-independent in
  // terms of per-second sensitivity (K*V constant). Do NOT normalize by
  // TimeDt here: in an uncapped game that makes sensitivity scale linearly
  // with framerate (4x faster look at 240 FPS vs 60 FPS).
  rav += static_cast<float>((ms.x-VideoCX)) * (OptMsSens+64) / 600.f / 192.f;
  rbv += static_cast<float>((ms.y-VideoCY)) * (OptMsSens+64) / 600.f / 192.f;
//  if (KeyFlags & kfStrafe)
//    SSpeed+= static_cast<float>(rav) * 10;
//  else
    PlayerAlpha += rav;
  PlayerBeta  += rbv;

  // Per-second exponential decay (10 ms time constant) so the smoothing is
  // framerate-independent. Replaces the old per-frame `/(2 + TimeDt/20)`
  // which was much stronger at low FPS and made the look sluggish on slow
  // frames.
  float decay = expf(-static_cast<float>(TimeDt) / 10.0f);
  rav *= decay;
  rbv *= decay;
  ResetMousePos();



  if ( !(KeyFlags & (kfForward | kfBackward)))
    if (VSpeed>0) VSpeed=MAX(0,VSpeed-DeltaT*2);
    else VSpeed=MIN(0,VSpeed+DeltaT*2);

  if ( !(KeyFlags & (kfSLeft | kfSRight)))
    if (SSpeed>0) SSpeed=MAX(0,SSpeed-DeltaT*2);
    else SSpeed=MIN(0,SSpeed+DeltaT*2);

  if (KeyFlags & kfForward)  if (VSpeed>0) VSpeed+=DeltaT;
    else VSpeed+=DeltaT*4;
  if (KeyFlags & kfBackward) if (VSpeed<0) VSpeed-=DeltaT;
    else VSpeed-=DeltaT*4;

  if (KeyFlags & kfSRight )  if (SSpeed>0) SSpeed+=DeltaT;
    else SSpeed+=DeltaT*4;
  if (KeyFlags & kfSLeft  )  if (SSpeed<0) SSpeed-=DeltaT;
    else SSpeed-=DeltaT*4;


  if (g_GameMode == GameMode::Swimming)
  {
    if (VSpeed > 0.25f) VSpeed = 0.25f;
    if (VSpeed <-0.25f) VSpeed =-0.25f;
    if (SSpeed > 0.25f) SSpeed = 0.25f;
    if (SSpeed <-0.25f) SSpeed =-0.25f;
  }
  if ( RunMode && (HeadY == 220.f) && (Weapon.state==0 || WeapInfo[CurrentWeapon].canRun))
  {
    if (VSpeed > 0.7f) VSpeed = 0.7f;
    if (VSpeed <-0.7f) VSpeed =-0.7f;
    if (SSpeed > 0.7f) SSpeed = 0.7f;
    if (SSpeed <-0.7f) SSpeed =-0.7f;
  }
  else
  {
    if (VSpeed > 0.3f) VSpeed = 0.3f;
    if (VSpeed <-0.3f) VSpeed =-0.3f;
    if (SSpeed > 0.30f) SSpeed = 0.30f;
    if (SSpeed <-0.30f) SSpeed =-0.30f;
  }

  if (KeyboardState[KeyMap.fkFire] & 128) {
	  ProcessShoot();
	  alreadyFired = true;
  } else alreadyFired = false;

  //STRAFE - PUMP
//  if (KeyFlags & kfStrafe)
  if (KeyboardState[KeyMap.fkStrafe] & 128) ProcessPump(); 

  //FIRING MODE
  if (KeyboardState[KeyMap.fkFiringMode] & 128) ProcessFireMode();

  if (KeyboardState[KeyMap.fkReload] & 128) ProcessReload();

  //menu option/already used check needed - TODO
  if (KeyboardState[KeyMap.fkResupply] & 128) AddShipSupply(PlayerX,PlayerZ);

  if (Weapon.state) {
	  if (KeyboardState[KeyMap.fkHoldBreath] & 128 && !IsUnderwater()) {
		  if (Weapon.breathPressed == 0) {
			  AddVoicev(fxBreathIn.length, fxBreathIn.lpData.data(), 256);
			  Weapon.breathPressed = 1;
		  }
		  if (!Weapon.HoldBreath) {
			  Weapon.HoldBreath = true;
		  }
	  }
	  else {
		  if (Weapon.HoldBreath) {
			  Weapon.HoldBreath = false;
			  if (Weapon.breathPressed == 1 && !IsUnderwater()) AddVoicev(fxBreathOut.length, fxBreathOut.lpData.data(), 256);
		  }
		  Weapon.breathPressed = 0;
	  }
  }

  if (Multiplayer && Host) {
	  for (int c = 0; c < 6; c++) {
		  if (mDamage[0][c]) {
			  Characters[c].Health -= mDamage[0][c];
			  mDamage[0][c] = 0;
			  if (Characters[c].Health < 0) Characters[c].Health = 0;
			  registerDamage(c, false);// this needs to register enemy damage!
		  }
	  }
  }

  if (KeyboardState[VK_RETURN] & 128) if (TrophyDisplay && !ScoreDispTime && !Characters[TrophyDisplayC].claimed && !Tranq) AddShipTask(TrophyDisplayC);

  if (KeyboardState [KeyMap.fkShow] & 128) HideWeapon();

  if (g_GameMode == GameMode::Binocular)
  {
    if (KeyboardState[VK_ADD     ] & 128) BinocularPower+=BinocularPower * TimeDt / 4000.f;
    if (KeyboardState[VK_SUBTRACT] & 128) BinocularPower-=BinocularPower * TimeDt / 4000.f;
    if (BinocularPower < 1.5f) BinocularPower = 1.5f;
    if (BinocularPower > 3.0f) BinocularPower = 3.0f;
  }

  if (g_GameMode == GameMode::OpticScope)
  {
    // Breath-driven aim zoom. Scoped weapons rest at their optic value
    // (the scope mask is authored to fill the screen at that
    // magnification, so going below it would reveal the mask border) and
    // holding breath eases toward a 1.5x focus ceiling (stock sniper:
    // 3.0x -> 4.5x). Breath-aim weapons (stock rifle) rest unzoomed and
    // only reach their optic while held (1.0x -> 1.6x); releasing eases
    // back. The hold is self-limiting: Hunt.cpp caps Weapon.BTime at ~4s
    // and then forces an exhale, so zoom can never be parked. No free
    // Numpad zoom on purpose — one mechanic (focus), one sensible cap.
    const float opticFloor = (WeapInfo[CurrentWeapon].Optic > 1.0f) ? WeapInfo[CurrentWeapon].Optic : 1.0f;
    const bool breathAim = WeapInfo[CurrentWeapon].breathaim;
    const float restLevel = breathAim ? 1.0f : opticFloor;
    const float focusCeil = breathAim ? opticFloor : opticFloor * 1.5f;
    const float target = (Weapon.HoldBreath ? focusCeil : restLevel);
    // Slow exponential approach (~900ms time constant): a deliberate focus
    // pull, not a snap. Frame-rate independent via TimeDt.
    const float k = 1.f - expf(-static_cast<float>(TimeDt) / 900.f);
    ScopePower += (target - ScopePower) * k;
    // Hard clamp: also repairs any stale wide zoom (e.g. 10x from older
    // Numpad-zoom builds) the first scoped frame after loading.
    if (ScopePower < restLevel) ScopePower = restLevel;
    if (ScopePower > focusCeil) ScopePower = focusCeil;
  }

  if (KeyFlags & kfCall) MakeCall();

  if (DEBUG)
    if (KeyboardState [VK_CONTROL] & 128)
      if (KeyFlags & kfBackward) VSpeed =-8;
      else VSpeed = 8;

  if (KeyFlags & kfJump)
    if (YSpeed == 0 && g_GameMode != GameMode::Swimming)
    {
      YSpeed = 600 + static_cast<float>(fabs(VSpeed)) * 600;
      AddVoicev(fxJump.length, fxJump.lpData.data(), 256);
    }

//=========  rotation =========//
  if (KeyFlags & kfRight)  PlayerAlpha+=DeltaT*1.5f;
  if (KeyFlags & kfLeft )  PlayerAlpha-=DeltaT*1.5f;
//  if (KeyFlags & kfLookUp) PlayerBeta-=DeltaT;
//  if (KeyFlags & kfLookDn) PlayerBeta+=DeltaT;

//========= movement ==========//

  ca = static_cast<float>(cos(PlayerAlpha));
  sa = static_cast<float>(sin(PlayerAlpha));
  cb = static_cast<float>(cos(PlayerBeta));
  sb = static_cast<float>(sin(PlayerBeta));

  nv.x=sa;
  nv.y=0;
  nv.z=-ca;


  PlayerNv = nv;
  if (IsUnderwater() || FLY)
  {
    nv.x*=cb;
    nv.y=-sb;
    nv.z*=cb;
    PlayerNv = nv;
  }
  else
  {
    PlayerNv.x*=cb;
    PlayerNv.y=-sb;
    PlayerNv.z*=cb;
  }

  Vector3d sv = nv;
  nv.x*=static_cast<float>(TimeDt)*VSpeed;
  nv.y*=static_cast<float>(TimeDt)*VSpeed;
  nv.z*=static_cast<float>(TimeDt)*VSpeed;

  sv.x*=static_cast<float>(TimeDt)*SSpeed;
  sv.y=0;
  sv.z*=static_cast<float>(TimeDt)*SSpeed;

  if (g_GameMode != GameMode::TrophyMode)
  {
    TrophyRoom.Last.path+=(TimeDt*VSpeed) / 128.f;
    TrophyRoom.Last.time+=TimeDt/1000.f;
  }

//if (SWIM & (VSpeed>0.1) & (sb>0.60)) HeadY-=40;

  int mvi = 1 + TimeDt / 16;

  for (int mvc = 0; mvc<mvi; mvc++)
  {
    PlayerX+=nv.x / mvi;
    PlayerY+=nv.y / mvi;
    PlayerZ+=nv.z / mvi;

    PlayerX-=sv.z / mvi;
    PlayerZ+=sv.x / mvi;

    if (!NOCLIP) CheckCollision(PlayerX, PlayerZ);

    if (PlayerY <= GetLandQHNoObj(PlayerX, PlayerZ)+16)
    {
      ProcessSlide();
      ProcessSlide();
    }
  }

  if (PlayerY <= GetLandQHNoObj(PlayerX, PlayerZ)+16)
  {
    ProcessSlide();
    ProcessSlide();
  }
//===========================================================
}