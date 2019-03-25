#include "r_local.h"

// not really draw alias models here, but use this to draw triangles

int				r_amodels_drawn;

affinetridesc_t	r_affinetridesc;

vec3_t			r_plightvec;
vec3_t          r_lerped[1024];
vec3_t          r_lerp_frontv, r_lerp_backv, r_lerp_move;

int				r_ambientlight;
int				r_aliasblendcolor;
float			r_shadelight;


float	aliastransform[3][4];
float   aliasworldtransform[3][4];
float   aliasoldworldtransform[3][4];

static float	s_ziscale;
static vec3_t	s_alias_forward, s_alias_right, s_alias_up;


#define NUMVERTEXNORMALS	162

float	r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};


void R_AliasSetUpTransform (void);
void R_AliasTransformVector (vec3_t in, vec3_t out, float m[3][4] );
void R_AliasProjectAndClipTestFinalVert (finalvert_t *fv);

void R_AliasTransformFinalVerts( int numpoints, finalvert_t *fv, dtrivertx_t *oldv, dtrivertx_t *newv );


/*
================
R_AliasCheckBBox
================
*/
typedef struct {
	int	index0;
	int	index1;
} aedge_t;

static aedge_t	aedges[12] = {
{0, 1}, {1, 2}, {2, 3}, {3, 0},
{4, 5}, {5, 6}, {6, 7}, {7, 4},
{0, 5}, {1, 4}, {2, 7}, {3, 6}
};

#define BBOX_TRIVIAL_ACCEPT 0
#define BBOX_MUST_CLIP_XY   1
#define BBOX_MUST_CLIP_Z    2
#define BBOX_TRIVIAL_REJECT 8


/*
================
R_AliasTransformVector
================
*/
void R_AliasTransformVector(vec3_t in, vec3_t out, float xf[3][4] )
{
	out[0] = DotProduct(in, xf[0]) + xf[0][3];
	out[1] = DotProduct(in, xf[1]) + xf[1][3];
	out[2] = DotProduct(in, xf[2]) + xf[2][3];
}

void VectorInverse (vec3_t v)
{
	v[0] = -v[0];
	v[1] = -v[1];
	v[2] = -v[2];
}

/*
================
R_AliasSetUpTransform
================
*/
void R_AliasSetUpTransform (void)
{
	int				i;
	static float	viewmatrix[3][4];
	vec3_t			angles;

// TODO: should really be stored with the entity instead of being reconstructed
// TODO: should use a look-up table
// TODO: could cache lazily, stored in the entity
//

	s_ziscale = (float)0x8000 * (float)0x10000;
	angles[ROLL] = RI.currententity->angles[ROLL];
	angles[PITCH] = RI.currententity->angles[PITCH];
	angles[YAW] = RI.currententity->angles[YAW];
	AngleVectors( angles, s_alias_forward, s_alias_right, s_alias_up );

// TODO: can do this with simple matrix rearrangement

	memset( aliasworldtransform, 0, sizeof( aliasworldtransform ) );
	memset( aliasoldworldtransform, 0, sizeof( aliasworldtransform ) );

	for (i=0 ; i<3 ; i++)
	{
		aliasoldworldtransform[i][0] = aliasworldtransform[i][0] =  s_alias_forward[i];
		aliasoldworldtransform[i][0] = aliasworldtransform[i][1] = -s_alias_right[i];
		aliasoldworldtransform[i][0] = aliasworldtransform[i][2] =  s_alias_up[i];
	}

	aliasworldtransform[0][3] = RI.currententity->origin[0]-r_origin[0];
	aliasworldtransform[1][3] = RI.currententity->origin[1]-r_origin[1];
	aliasworldtransform[2][3] = RI.currententity->origin[2]-r_origin[2];

	//aliasoldworldtransform[0][3] = RI.currententity->oldorigin[0]-r_origin[0];
	//aliasoldworldtransform[1][3] = RI.currententity->oldorigin[1]-r_origin[1];
	//aliasoldworldtransform[2][3] = RI.currententity->oldorigin[2]-r_origin[2];

// FIXME: can do more efficiently than full concatenation
//	memcpy( rotationmatrix, t2matrix, sizeof( rotationmatrix ) );

//	R_ConcatTransforms (t2matrix, tmatrix, rotationmatrix);

// TODO: should be global, set when vright, etc., set
	VectorCopy (vright, viewmatrix[0]);
	VectorCopy (vup, viewmatrix[1]);
	VectorInverse (viewmatrix[1]);
	//VectorScale(viewmatrix[1], -1, viewmatrix[1]);
	VectorCopy (vpn, viewmatrix[2]);

	viewmatrix[0][3] = 0;
	viewmatrix[1][3] = 0;
	viewmatrix[2][3] = 0;

//	memcpy( aliasworldtransform, rotationmatrix, sizeof( aliastransform ) );

	R_ConcatTransforms (viewmatrix, aliasworldtransform, aliastransform);

	aliasworldtransform[0][3] = RI.currententity->origin[0];
	aliasworldtransform[1][3] = RI.currententity->origin[1];
	aliasworldtransform[2][3] = RI.currententity->origin[2];

	//aliasoldworldtransform[0][3] = RI.currententity->oldorigin[0];
	//aliasoldworldtransform[1][3] = RI.currententity->oldorigin[1];
	//aliasoldworldtransform[2][3] = RI.currententity->oldorigin[2];
}

/*
================
R_AliasProjectAndClipTestFinalVert
================
*/
void R_AliasProjectAndClipTestFinalVert( finalvert_t *fv )
{
	float	zi;
	float	x, y, z;

	// project points
	x = fv->xyz[0];
	y = fv->xyz[1];
	z = fv->xyz[2];
	zi = 1.0 / z;

	fv->zi = zi * s_ziscale;

	fv->u = (x * aliasxscale * zi) + aliasxcenter;
	fv->v = (y * aliasyscale * zi) + aliasycenter;

	if (fv->u < RI.aliasvrect.x)
		fv->flags |= ALIAS_LEFT_CLIP;
	if (fv->v < RI.aliasvrect.y)
		fv->flags |= ALIAS_TOP_CLIP;
	if (fv->u > RI.aliasvrectright)
		fv->flags |= ALIAS_RIGHT_CLIP;
	if (fv->v > RI.aliasvrectbottom)
		fv->flags |= ALIAS_BOTTOM_CLIP;
}

void R_SetupFinalVert( finalvert_t *fv, float x, float y, float z, int light, int s, int t )
{
	vec3_t v = {x, y, z};

	fv->xyz[0] = DotProduct(v, aliastransform[0]) + aliastransform[0][3];
	fv->xyz[1] = DotProduct(v, aliastransform[1]) + aliastransform[1][3];
	fv->xyz[2] = DotProduct(v, aliastransform[2]) + aliastransform[2][3];

	fv->flags = 0;

	fv->l = light;

	if ( fv->xyz[2] < ALIAS_Z_CLIP_PLANE )
	{
		fv->flags |= ALIAS_Z_CLIP;
	}
	else
	{
		R_AliasProjectAndClipTestFinalVert( fv );
	}

	fv->s = s << 16;
	fv->t = t << 16;
}

void R_RenderTriangle( finalvert_t *pfv )
{

	if ( pfv[0].flags & pfv[1].flags & pfv[2].flags )
		return ;		// completely clipped

	if ( ! (pfv[0].flags | pfv[1].flags | pfv[2].flags) )
	{	// totally unclipped
		aliastriangleparms.a = &pfv[0];
		aliastriangleparms.b = &pfv[1];
		aliastriangleparms.c = &pfv[2];

		R_DrawTriangle();
	}
	else
	{	// partially clipped
		R_AliasClipTriangle (&pfv[0], &pfv[1], &pfv[2]);
	}
}



