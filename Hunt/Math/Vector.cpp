// Vector.cpp — auto-extracted from Math.cpp
// ==========================================================================
// Auto-extracted from Math.cpp
// ==========================================================================

#include "Hunt.h"

static float FastInvSqrt(float x)
{
  float xhalf = 0.5f * x;
  union {
    float f;
    int i;
  } uf;
  uf.f = x;
  uf.i = 0x5f3759df - (uf.i >> 1);
  uf.f *= (1.5f - (xhalf * uf.f * uf.f));
  return uf.f;
}
void NormVector(Vector3d& v, float Scale)
{
  double n;
  float factor;
  n=v.x*v.x + v.y*v.y + v.z*v.z;
  if (n<0.000000001) n=0.000000001;
  if (Scale == 1.0f) {
    factor = FastInvSqrt(static_cast<float>(n));
  } else {
    factor = static_cast<float>(static_cast<double>(Scale) / sqrt(n));
  }
  v.x=v.x*factor;
  v.y=v.y*factor;
  v.z=v.z*factor;
}
float SGN(float f)
{
  if (f<0) return -1.f;
  else return  1.f;
}
void DeltaFunc(float &a, float b, float d)
{
  if (b > a)
  {
    a+=d;
    if (a > b) a = b;
  }
  else
  {
    a-=d;
    if (a < b) a = b;
  }
}
void MulVectorsScal(const Vector3d& v1, const Vector3d& v2, float& r)
{
  r = v1.x*v2.x + v1.y*v2.y + v1.z*v2.z;
}
void MulVectorsVect(const Vector3d& v1, const Vector3d& v2, Vector3d& r )
{
  r.x= v1.y*v2.z - v2.y*v1.z;
  r.y=-v1.x*v2.z + v2.x*v1.z;
  r.z= v1.x*v2.y - v2.x*v1.y;
}

Vector3d RotateVector(Vector3d& v)
{
  Vector3d vv;
  float vx = v.x * ca + v.z * sa;
  float vz = v.z * ca - v.x * sa;
  float vy = v.y;
  vv.x = vx;
  vv.y = vy * cb - vz * sb;
  vv.z = vz * cb + vy * sb;
  return vv;
}

Vector3d SubVectors2d(Vector3d& v1, Vector3d& v2)
{
	Vector3d res;
	res.x = v1.x - v2.x;
	res.y = 0;
	res.z = v1.z - v2.z;
	return res;
}

Vector3d SubVectors( Vector3d& v1, Vector3d& v2 )
{
  Vector3d res;
  res.x = v1.x-v2.x;
  res.y = v1.y-v2.y;
  res.z = v1.z-v2.z;
  return res;
}

Vector3d AddVectors( Vector3d& v1, Vector3d& v2 )
{
  Vector3d res;
  res.x = v1.x+v2.x;
  res.y = v1.y+v2.y;
  res.z = v1.z+v2.z;
  return res;
}
float VectorLengthSq(Vector3d v)
{
  return v.x*v.x + v.y*v.y + v.z*v.z;
}
float VectorLength(Vector3d v)
{
  return static_cast<float>(sqrt(VectorLengthSq(v)));
}
int siRand(int R)
{
  if (R == RAND_MAX) return 0;
  return static_cast<int>((static_cast<std::int64_t>(rand()) * (static_cast<std::int64_t>(R) * 2 + 1)) / RAND_MAX - R);
}
int rRand(int r)
{
  if (!r) return 0;
  int res = rand() % (r+1);// / (RAND_MAX);
//if (res > r) res = r;
  return res;
}
float FindVectorAlpha(float vx, float vy)
{
  float alpha = atan2f(vy, vx);
  if (alpha < 0) alpha += 2.f * pi;
  return alpha;
}
void CalcHitPoint(CLIPPLANE& C, Vector3d& a, Vector3d& b, Vector3d& hp)
{
  float SCLN,SCVN;
  Vector3d lv = SubVectors(b,a);
  NormVector(lv, 1.0);

  MulVectorsScal(a, C.nv, SCLN);
  MulVectorsScal(lv,C.nv, SCVN);

  SCLN/=SCVN;
  SCLN=static_cast<float>(fabs(SCLN));
  hp.x = a.x + lv.x * SCLN;
  hp.y = a.y + lv.y * SCLN;
  hp.z = a.z + lv.z * SCLN;
}
float PointToVectorDSq(Vector3d A, Vector3d AB, Vector3d C)
{
  Vector3d AC = SubVectors(C,A);
  Vector3d vm;
  MulVectorsVect(AB, AC, vm);
  return vm.x*vm.x + vm.y*vm.y + vm.z*vm.z;
}
float PointToVectorD(Vector3d A, Vector3d AB, Vector3d C)
{
  return static_cast<float>(sqrt(PointToVectorDSq(A, AB, C)));
}
float Mul2dVectors(float vx, float vy, float ux, float uy)
{
  return vx*uy - ux*vy;
}
