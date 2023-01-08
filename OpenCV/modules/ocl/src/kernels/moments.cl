#if defined (DOUBLE_SUPPORT)
#pragma OPENCL EXTENSION cl_khr_fp64:enable
#else
typedef float double;
typedef float4 double4;
#define convert_double4 convert_float4
#endif
//#pragma OPENCL EXTENSION cl_amd_printf:enable
//#if defined (DOUBLE_SUPPORT)
__kernel void icvContourMoments(int contour_total, 
								__global float* reader_oclmat_data, 
                                __global double* dst_a00,
                                __global double* dst_a10,
                                __global double* dst_a01,
                                __global double* dst_a20,
                                __global double* dst_a11,
                                __global double* dst_a02,
                                __global double* dst_a30,
                                __global double* dst_a21,
                                __global double* dst_a12,
                                __global double* dst_a03)
{
    double xi_1, yi_1, xi_12, yi_12, xi, yi, xi2, yi2, dxy, xii_1, yii_1;
    int idx = get_global_id(0);

    xi_1 = *(reader_oclmat_data + (get_global_id(0) << 1));
    yi_1 = *(reader_oclmat_data + (get_global_id(0) << 1) + 1);
    xi_12 = xi_1 * xi_1;
    yi_12 = yi_1 * yi_1;

    if(idx == contour_total - 1)
    {
        xi = *(reader_oclmat_data);
        yi = *(reader_oclmat_data + 1);
    }
    else
    {
        xi = *(reader_oclmat_data + (idx + 1) * 2);
        yi = *(reader_oclmat_data + (idx + 1) * 2 + 1);
    }

    xi2 = xi * xi;
    yi2 = yi * yi;
    dxy = xi_1 * yi - xi * yi_1;
    xii_1 = xi_1 + xi;
    yii_1 = yi_1 + yi;

    dst_a00[idx] = dxy;
    dst_a10[idx] = dxy * xii_1;
    dst_a01[idx] = dxy * yii_1;
    dst_a20[idx] = dxy * (xi_1 * xii_1 + xi2);
    dst_a11[idx] = dxy * (xi_1 * (yii_1 + yi_1) + xi * (yii_1 + yi));
    dst_a02[idx] = dxy * (yi_1 * yii_1 + yi2);
    dst_a30[idx] = dxy * xii_1 * (xi_12 + xi2);
    dst_a03[idx] = dxy * yii_1 * (yi_12 + yi2);
    dst_a21[idx] =
        dxy * (xi_12 * (3 * yi_1 + yi) + 2 * xi * xi_1 * yii_1 +
               xi2 * (yi_1 + 3 * yi));
    dst_a12[idx] =
        dxy * (yi_12 * (3 * xi_1 + xi) + 2 * yi * yi_1 * xii_1 +
               yi2 * (xi_1 + 3 * xi));
}
//#endif

//#if defined (DOUBLE_SUPPORT)
__kernel void CvMoments_D0(__global uchar16* src_data, int src_rows, int src_cols, int src_step, int tileSize_width, int tileSize_height,
                           __global double* dst_m00,
                           __global double* dst_m10,
                           __global double* dst_m01,
                           __global double* dst_m20,
                           __global double* dst_m11,
                           __global double* dst_m02,
                           __global double* dst_m30,
                           __global double* dst_m21,
                           __global double* dst_m12,
                           __global double* dst_m03,
                           int dst_cols, int dst_step, int type, int depth, int cn, int coi, int binary, int TILE_SIZE)
{
    uchar tmp_coi[16]; // get the coi data
    uchar16 tmp[16];
    int VLEN_C = 16;  // vector length of uchar

    int gidy = get_global_id(0);
    int gidx = get_global_id(1);
    int wgidy = get_group_id(0);
    int wgidx = get_group_id(1);
    int lidy = get_local_id(0);
    int lidx = get_local_id(1);
    int y = wgidy*TILE_SIZE; // vector length of uchar
    int x = wgidx*TILE_SIZE;  // vector length of uchar
    int kcn = (cn==2)?2:4;
    int rstep = min(src_step, TILE_SIZE);
    tileSize_height = min(TILE_SIZE, src_rows - y);
    tileSize_width = min(TILE_SIZE, src_cols - x);

    if( tileSize_width < TILE_SIZE )
        for(int i = tileSize_width; i < rstep; i++ )
            *((__global uchar*)src_data+(y+lidy)*src_step+x+i) = 0;
    if( coi > 0 )	//channel of interest
        for(int i = 0; i < tileSize_width; i += VLEN_C)
        {
            for(int j=0; j<VLEN_C; j++)
                tmp_coi[j] = *((__global uchar*)src_data+(y+lidy)*src_step+(x+i+j)*kcn+coi-1);
            tmp[i/VLEN_C] = (uchar16)(tmp_coi[0],tmp_coi[1],tmp_coi[2],tmp_coi[3],tmp_coi[4],tmp_coi[5],tmp_coi[6],tmp_coi[7],
                                      tmp_coi[8],tmp_coi[9],tmp_coi[10],tmp_coi[11],tmp_coi[12],tmp_coi[13],tmp_coi[14],tmp_coi[15]);
        }
    else
        for(int i=0; i < tileSize_width; i+=VLEN_C)
            tmp[i/VLEN_C] = *(src_data+(y+lidy)*src_step/VLEN_C+(x+i)/VLEN_C);
    uchar16 zero = (uchar16)(0);
    uchar16 full = (uchar16)(255);
    if( binary )
        for(int i=0; i < tileSize_width; i+=VLEN_C)
            tmp[i/VLEN_C] = (tmp[i/VLEN_C]!=zero)?full:zero;
    double mom[10];
    __local int m[10][128];
    if(lidy == 0)
        for(int i=0; i<10; i++)
            for(int j=0; j<128; j++)
                m[i][j]=0;
    barrier(CLK_LOCAL_MEM_FENCE);
    int lm[10] = {0};
    int16 x0 = (int16)(0);
    int16 x1 = (int16)(0);
    int16 x2 = (int16)(0);
    int16 x3 = (int16)(0);
    for( int xt = 0 ; xt < tileSize_width; xt+=(VLEN_C) )
    {
        int16 v_xt = (int16)(xt, xt+1, xt+2, xt+3, xt+4, xt+5, xt+6, xt+7, xt+8, xt+9, xt+10, xt+11, xt+12, xt+13, xt+14, xt+15);
        int16 p = convert_int16(tmp[xt/VLEN_C]);
        int16 xp = v_xt * p, xxp = xp *v_xt;
        x0 += p;
        x1 += xp;
        x2 += xxp;
        x3 += xxp * v_xt;
    }
    x0.s0 += x0.s1 + x0.s2 + x0.s3 + x0.s4 + x0.s5 + x0.s6 + x0.s7 + x0.s8 + x0.s9 + x0.sa + x0.sb + x0.sc + x0.sd + x0.se + x0.sf;
    x1.s0 += x1.s1 + x1.s2 + x1.s3 + x1.s4 + x1.s5 + x1.s6 + x1.s7 + x1.s8 + x1.s9 + x1.sa + x1.sb + x1.sc + x1.sd + x1.se + x1.sf;
    x2.s0 += x2.s1 + x2.s2 + x2.s3 + x2.s4 + x2.s5 + x2.s6 + x2.s7 + x2.s8 + x2.s9 + x2.sa + x2.sb + x2.sc + x2.sd + x2.se + x2.sf;
    x3.s0 += x3.s1 + x3.s2 + x3.s3 + x3.s4 + x3.s5 + x3.s6 + x3.s7 + x3.s8 + x3.s9 + x3.sa + x3.sb + x3.sc + x3.sd + x3.se + x3.sf;
    int py = lidy * ((int)x0.s0);
    int sy = lidy*lidy;
    int bheight = min(tileSize_height, TILE_SIZE/2);
    if(bheight >= TILE_SIZE/2&&lidy > bheight-1&&lidy < tileSize_height)
    {
        m[9][lidy-bheight] = ((int)py) * sy;  // m03
        m[8][lidy-bheight] = ((int)x1.s0) * sy;  // m12
        m[7][lidy-bheight] = ((int)x2.s0) * lidy;  // m21
        m[6][lidy-bheight] = x3.s0;             // m30
        m[5][lidy-bheight] = x0.s0 * sy;        // m02
        m[4][lidy-bheight] = x1.s0 * lidy;         // m11
        m[3][lidy-bheight] = x2.s0;             // m20
        m[2][lidy-bheight] = py;             // m01
        m[1][lidy-bheight] = x1.s0;             // m10
        m[0][lidy-bheight] = x0.s0;             // m00
    }
    else if(lidy < bheight)
    {
        lm[9] = ((int)py) * sy;  // m03
        lm[8] = ((int)x1.s0) * sy;  // m12
        lm[7] = ((int)x2.s0) * lidy;  // m21
        lm[6] = x3.s0;             // m30
        lm[5] = x0.s0 * sy;        // m02
        lm[4] = x1.s0 * lidy;         // m11
        lm[3] = x2.s0;             // m20
        lm[2] = py;             // m01
        lm[1] = x1.s0;             // m10
        lm[0] = x0.s0;             // m00
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    for( int j = bheight; j >= 1; j = j/2 )
    {
        if(lidy < j)
            for( int i = 0; i < 10; i++ )
                lm[i] = lm[i] + m[i][lidy];
        barrier(CLK_LOCAL_MEM_FENCE);
        if(lidy >= j/2&&lidy < j)
            for( int i = 0; i < 10; i++ )
                m[i][lidy-j/2] = lm[i];
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    if(lidy == 0&&lidx == 0)
    {
        for( int mt = 0; mt < 10; mt++ )
            mom[mt] = (double)lm[mt];
        if(binary)
        {
            double s = 1./255;
            for( int mt = 0; mt < 10; mt++ )
                mom[mt] *= s;
        }
        double xm = x * mom[0], ym = y * mom[0];

        // accumulate moments computed in each tile

        // + m00 ( = m00' )
        dst_m00[wgidy*dst_cols+wgidx] = mom[0];

        // + m10 ( = m10' + x*m00' )
        dst_m10[wgidy*dst_cols+wgidx]  = mom[1] + xm;

        // + m01 ( = m01' + y*m00' )
        dst_m01[wgidy*dst_cols+wgidx]  = mom[2] + ym;

        // + m20 ( = m20' + 2*x*m10' + x*x*m00' )
        dst_m20[wgidy*dst_cols+wgidx]  = mom[3] + x * (mom[1] * 2 + xm);

        // + m11 ( = m11' + x*m01' + y*m10' + x*y*m00' )
        dst_m11[wgidy*dst_cols+wgidx]  = mom[4] + x * (mom[2] + ym) + y * mom[1];

        // + m02 ( = m02' + 2*y*m01' + y*y*m00' )
        dst_m02[wgidy*dst_cols+wgidx]  = mom[5] + y * (mom[2] * 2 + ym);

        // + m30 ( = m30' + 3*x*m20' + 3*x*x*m10' + x*x*x*m00' )
        dst_m30[wgidy*dst_cols+wgidx]  = mom[6] + x * (3. * mom[3] + x * (3. * mom[1] + xm));

        // + m21 ( = m21' + x*(2*m11' + 2*y*m10' + x*m01' + x*y*m00') + y*m20')
        dst_m21[wgidy*dst_cols+wgidx]  = mom[7] + x * (2 * (mom[4] + y * mom[1]) + x * (mom[2] + ym)) + y * mom[3];

        // + m12 ( = m12' + y*(2*m11' + 2*x*m01' + y*m10' + x*y*m00') + x*m02')
        dst_m12[wgidy*dst_cols+wgidx]  = mom[8] + y * (2 * (mom[4] + x * mom[2]) + y * (mom[1] + xm)) + x * mom[5];

        // + m03 ( = m03' + 3*y*m02' + 3*y*y*m01' + y*y*y*m00' )
        dst_m03[wgidy*dst_cols+wgidx] = mom[9] + y * (3. * mom[5] + y * (3. * mom[2] + ym));
    }
}
//#endif
//#if defined (DOUBLE_SUPPORT)
__kernel void dst_sum(int src_rows, int src_cols, int tile_height, int tile_width, int TILE_SIZE, __global double* sum, __global double* dst_m00,
                      __global double* dst_m10,
                      __global double* dst_m01,
                      __global double* dst_m20,
                      __global double* dst_m11,
                      __global double* dst_m02,
                      __global double* dst_m30,
                      __global double* dst_m21,
                      __global double* dst_m12,
                      __global double* dst_m03)
{
    int gidy = get_global_id(0);
    int gidx = get_global_id(1);
    int block_y = src_rows/tile_height;
    int block_x = src_cols/tile_width;
    int block_num;

    if(src_rows > TILE_SIZE && src_rows % TILE_SIZE != 0)
        block_y ++;
    if(src_cols > TILE_SIZE && src_cols % TILE_SIZE != 0)
        block_x ++;
    block_num = block_y * block_x;
    __local double dst_sum[10][128];
    if(gidy<128-block_num)
        for(int i=0; i<10; i++)
            dst_sum[i][gidy+block_num]=0;
    barrier(CLK_LOCAL_MEM_FENCE);
    if(gidy<block_num)
    {
        dst_sum[0][gidy] = dst_m00[gidy];
        dst_sum[1][gidy] = dst_m10[gidy];
        dst_sum[2][gidy] = dst_m01[gidy];
        dst_sum[3][gidy] = dst_m20[gidy];
        dst_sum[4][gidy] = dst_m11[gidy];
        dst_sum[5][gidy] = dst_m02[gidy];
        dst_sum[6][gidy] = dst_m30[gidy];
        dst_sum[7][gidy] = dst_m21[gidy];
        dst_sum[8][gidy] = dst_m12[gidy];
        dst_sum[9][gidy] = dst_m03[gidy];
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    for(int lsize=64; lsize>0; lsize>>=1)
    {
        if(gidy<lsize)
        {
            int lsize2 = gidy + lsize;
            for(int i=0; i<10; i++)
                dst_sum[i][gidy] += dst_sum[i][lsize2];
        }
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    if(gidy==0)
        for(int i=0; i<10; i++)
            sum[i] = dst_sum[i][0];
}
//#endif
//#if defined (DOUBLE_SUPPORT)
__kernel void CvMoments_D2(__global ushort8* src_data, int src_rows, int src_cols, int src_step, int tileSize_width, int tileSize_height,
                           __global double* dst_m00,
                           __global double* dst_m10,
                           __global double* dst_m01,
                           __global double* dst_m20,
                           __global double* dst_m11,
                           __global double* dst_m02,
                           __global double* dst_m30,
                           __global double* dst_m21,
                           __global double* dst_m12,
                           __global double* dst_m03,
                           int dst_cols, int dst_step,
                           int type, int depth, int cn, int coi, int binary, const int TILE_SIZE)
{
    ushort tmp_coi[8]; // get the coi data
    ushort8 tmp[32];
    int VLEN_US = 8; // vector length of ushort
    int gidy = get_global_id(0);
    int gidx = get_global_id(1);
    int wgidy = get_group_id(0);
    int wgidx = get_group_id(1);
    int lidy = get_local_id(0);
    int lidx = get_local_id(1);
    int y = wgidy*TILE_SIZE;  // real Y index of pixel
    int x = wgidx*TILE_SIZE;  // real X index of pixel
    int kcn = (cn==2)?2:4;
    int rstep = min(src_step/2, TILE_SIZE);
    tileSize_height = min(TILE_SIZE, src_rows - y);
    tileSize_width = min(TILE_SIZE, src_cols -x);
    if(src_cols > TILE_SIZE && tileSize_width < TILE_SIZE)
        for(int i=tileSize_width; i < rstep; i++ )
            *((__global ushort*)src_data+(y+lidy)*src_step/2+x+i) = 0;
    if( coi > 0 )
        for(int i=0; i < tileSize_width; i+=VLEN_US)
        {
            for(int j=0; j<VLEN_US; j++)
                tmp_coi[j] = *((__global ushort*)src_data+(y+lidy)*(int)src_step/2+(x+i+j)*kcn+coi-1);
            tmp[i/VLEN_US] = (ushort8)(tmp_coi[0],tmp_coi[1],tmp_coi[2],tmp_coi[3],tmp_coi[4],tmp_coi[5],tmp_coi[6],tmp_coi[7]);
        }
    else
        for(int i=0; i < tileSize_width; i+=VLEN_US)
            tmp[i/VLEN_US] = *(src_data+(y+lidy)*src_step/(2*VLEN_US)+(x+i)/VLEN_US);
    ushort8 zero = (ushort8)(0);
    ushort8 full = (ushort8)(255);
    if( binary )
        for(int i=0; i < tileSize_width; i+=VLEN_US)
            tmp[i/VLEN_US] = (tmp[i/VLEN_US]!=zero)?full:zero;
    double mom[10];
    __local long m[10][128];
    if(lidy == 0)
        for(int i=0; i<10; i++)
            for(int j=0; j<128; j++)
                m[i][j]=0;
    barrier(CLK_LOCAL_MEM_FENCE);
    long lm[10] = {0};
    int8 x0 = (int8)(0);
    int8 x1 = (int8)(0);
    int8 x2 = (int8)(0);
    long8 x3 = (long8)(0);
    for( int xt = 0 ; xt < tileSize_width; xt+=(VLEN_US) )
    {
        int8 v_xt = (int8)(xt, xt+1, xt+2, xt+3, xt+4, xt+5, xt+6, xt+7);
        int8 p = convert_int8(tmp[xt/VLEN_US]);
        int8 xp = v_xt * p, xxp = xp * v_xt;
        x0 += p;
        x1 += xp;
        x2 += xxp;
        x3 += convert_long8(xxp) *convert_long8(v_xt);
    }
    x0.s0 += x0.s1 + x0.s2 + x0.s3 + x0.s4 + x0.s5 + x0.s6 + x0.s7;
    x1.s0 += x1.s1 + x1.s2 + x1.s3 + x1.s4 + x1.s5 + x1.s6 + x1.s7;
    x2.s0 += x2.s1 + x2.s2 + x2.s3 + x2.s4 + x2.s5 + x2.s6 + x2.s7;
    x3.s0 += x3.s1 + x3.s2 + x3.s3 + x3.s4 + x3.s5 + x3.s6 + x3.s7;

    int py = lidy * x0.s0, sy = lidy*lidy;
    int bheight = min(tileSize_height, TILE_SIZE/2);
    if(bheight >= TILE_SIZE/2&&lidy > bheight-1&&lidy < tileSize_height)
    {
        m[9][lidy-bheight] = ((long)py) * sy;  // m03
        m[8][lidy-bheight] = ((long)x1.s0) * sy;  // m12
        m[7][lidy-bheight] = ((long)x2.s0) * lidy;  // m21
        m[6][lidy-bheight] = x3.s0;             // m30
        m[5][lidy-bheight] = x0.s0 * sy;        // m02
        m[4][lidy-bheight] = x1.s0 * lidy;         // m11
        m[3][lidy-bheight] = x2.s0;             // m20
        m[2][lidy-bheight] = py;             // m01
        m[1][lidy-bheight] = x1.s0;             // m10
        m[0][lidy-bheight] = x0.s0;             // m00
    }
    else if(lidy < bheight)
    {
        lm[9] = ((long)py) * sy;  // m03
        lm[8] = ((long)x1.s0) * sy;  // m12
        lm[7] = ((long)x2.s0) * lidy;  // m21
        lm[6] = x3.s0;             // m30
        lm[5] = x0.s0 * sy;        // m02
        lm[4] = x1.s0 * lidy;         // m11
        lm[3] = x2.s0;             // m20
        lm[2] = py;             // m01
        lm[1] = x1.s0;             // m10
        lm[0] = x0.s0;             // m00
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    for( int j = TILE_SIZE/2; j >= 1; j = j/2 )
    {
        if(lidy < j)
            for( int i = 0; i < 10; i++ )
                lm[i] = lm[i] + m[i][lidy];
        barrier(CLK_LOCAL_MEM_FENCE);
        if(lidy >= j/2&&lidy < j)
            for( int i = 0; i < 10; i++ )
                m[i][lidy-j/2] = lm[i];
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    if(lidy == 0&&lidx == 0)
    {
        for(int mt = 0; mt < 10; mt++ )
            mom[mt] = (double)lm[mt];

        if(binary)
        {
            double s = 1./255;
            for( int mt = 0; mt < 10; mt++ )
                mom[mt] *= s;
        }

        double xm = x  *mom[0], ym = y * mom[0];

        // accumulate moments computed in each tile

        // + m00 ( = m00' )
        dst_m00[wgidy*dst_cols+wgidx] = mom[0];

        // + m10 ( = m10' + x*m00' )
        dst_m10[wgidy*dst_cols+wgidx]  = mom[1] + xm;

        // + m01 ( = m01' + y*m00' )
        dst_m01[wgidy*dst_cols+wgidx]  = mom[2] + ym;

        // + m20 ( = m20' + 2*x*m10' + x*x*m00' )
        dst_m20[wgidy*dst_cols+wgidx]  = mom[3] + x * (mom[1] * 2 + xm);

        // + m11 ( = m11' + x*m01' + y*m10' + x*y*m00' )
        dst_m11[wgidy*dst_cols+wgidx]  = mom[4] + x * (mom[2] + ym) + y * mom[1];

        // + m02 ( = m02' + 2*y*m01' + y*y*m00' )
        dst_m02[wgidy*dst_cols+wgidx]  = mom[5] + y * (mom[2] * 2 + ym);

        // + m30 ( = m30' + 3*x*m20' + 3*x*x*m10' + x*x*x*m00' )
        dst_m30[wgidy*dst_cols+wgidx]  = mom[6] + x * (3. * mom[3] + x * (3. * mom[1] + xm));

        // + m21 ( = m21' + x*(2*m11' + 2*y*m10' + x*m01' + x*y*m00') + y*m20')
        dst_m21[wgidy*dst_cols+wgidx] = mom[7] + x * (2 * (mom[4] + y * mom[1]) + x * (mom[2] + ym)) + y * mom[3];

        // + m12 ( = m12' + y*(2*m11' + 2*x*m01' + y*m10' + x*y*m00') + x*m02')
        dst_m12[wgidy*dst_cols+wgidx]  = mom[8] + y * (2 * (mom[4] + x * mom[2]) + y * (mom[1] + xm)) + x * mom[5];

        // + m03 ( = m03' + 3*y*m02' + 3*y*y*m01' + y*y*y*m00' )
        dst_m03[wgidy*dst_cols+wgidx]  = mom[9] + y * (3. * mom[5] + y * (3. * mom[2] + ym));
    }
}
//#endif
//#if defined (DOUBLE_SUPPORT)
__kernel void CvMoments_D3(__global short8* src_data, int src_rows, int src_cols, int src_step, int tileSize_width, int tileSize_height,
                           __global double* dst_m00,
                           __global double* dst_m10,
                           __global double* dst_m01,
                           __global double* dst_m20,
                           __global double* dst_m11,
                           __global double* dst_m02,
                           __global double* dst_m30,
                           __global double* dst_m21,
                           __global double* dst_m12,
                           __global double* dst_m03,
                           int dst_cols, int dst_step,
                           int type, int depth, int cn, int coi, int binary, const int TILE_SIZE)
{
    short tmp_coi[8]; // get the coi data
    short8 tmp[32];
    int VLEN_S =8; // vector length of short
    int gidy = get_global_id(0);
    int gidx = get_global_id(1);
    int wgidy = get_group_id(0);
    int wgidx = get_group_id(1);
    int lidy = get_local_id(0);
    int lidx = get_local_id(1);
    int y = wgidy*TILE_SIZE;  // real Y index of pixel
    int x = wgidx*TILE_SIZE;  // real X index of pixel
    int kcn = (cn==2)?2:4;
    int rstep = min(src_step/2, TILE_SIZE);
    tileSize_height = min(TILE_SIZE, src_rows - y);
    tileSize_width = min(TILE_SIZE, src_cols -x);
    if(tileSize_width < TILE_SIZE)
        for(int i = tileSize_width; i < rstep; i++ )
            *((__global short*)src_data+(y+lidy)*src_step/2+x+i) = 0;
    if( coi > 0 )
        for(int i=0; i < tileSize_width; i+=VLEN_S)
        {
            for(int j=0; j<VLEN_S; j++)
                tmp_coi[j] = *((__global short*)src_data+(y+lidy)*src_step/2+(x+i+j)*kcn+coi-1);
            tmp[i/VLEN_S] = (short8)(tmp_coi[0],tmp_coi[1],tmp_coi[2],tmp_coi[3],tmp_coi[4],tmp_coi[5],tmp_coi[6],tmp_coi[7]);
        }
    else
        for(int i=0; i < tileSize_width; i+=VLEN_S)
            tmp[i/VLEN_S] = *(src_data+(y+lidy)*src_step/(2*VLEN_S)+(x+i)/VLEN_S);
    short8 zero = (short8)(0);
    short8 full = (short8)(255);
    if( binary )
        for(int i=0; i < tileSize_width; i+=(VLEN_S))
            tmp[i/VLEN_S] = (tmp[i/VLEN_S]!=zero)?full:zero;

    double mom[10];
    __local long m[10][128];
    if(lidy == 0)
        for(int i=0; i<10; i++)
            for(int j=0; j<128; j++)
                m[i][j]=0;
    barrier(CLK_LOCAL_MEM_FENCE);
    long lm[10] = {0};
    int8 x0 = (int8)(0);
    int8 x1 = (int8)(0);
    int8 x2 = (int8)(0);
    long8 x3 = (long8)(0);
    for( int xt = 0 ; xt < tileSize_width; xt+= (VLEN_S))
    {
        int8 v_xt = (int8)(xt, xt+1, xt+2, xt+3, xt+4, xt+5, xt+6, xt+7);
        int8 p = convert_int8(tmp[xt/VLEN_S]);
        int8 xp = v_xt * p, xxp = xp * v_xt;
        x0 += p;
        x1 += xp;
        x2 += xxp;
        x3 += convert_long8(xxp) * convert_long8(v_xt);
    }
    x0.s0 += x0.s1 + x0.s2 + x0.s3 + x0.s4 + x0.s5 + x0.s6 + x0.s7;
    x1.s0 += x1.s1 + x1.s2 + x1.s3 + x1.s4 + x1.s5 + x1.s6 + x1.s7;
    x2.s0 += x2.s1 + x2.s2 + x2.s3 + x2.s4 + x2.s5 + x2.s6 + x2.s7;
    x3.s0 += x3.s1 + x3.s2 + x3.s3 + x3.s4 + x3.s5 + x3.s6 + x3.s7;

    int py = lidy * x0.s0, sy = lidy*lidy;
    int bheight = min(tileSize_height, TILE_SIZE/2);
    if(bheight >= TILE_SIZE/2&&lidy > bheight-1&&lidy < tileSize_height)
    {
        m[9][lidy-bheight] = ((long)py) * sy;  // m03
        m[8][lidy-bheight] = ((long)x1.s0) * sy;  // m12
        m[7][lidy-bheight] = ((long)x2.s0) * lidy;  // m21
        m[6][lidy-bheight] = x3.s0;             // m30
        m[5][lidy-bheight] = x0.s0 * sy;        // m02
        m[4][lidy-bheight] = x1.s0 * lidy;         // m11
        m[3][lidy-bheight] = x2.s0;             // m20
        m[2][lidy-bheight] = py;             // m01
        m[1][lidy-bheight] = x1.s0;             // m10
        m[0][lidy-bheight] = x0.s0;             // m00
    }
    else if(lidy < bheight)
    {
        lm[9] = ((long)py) * sy;  // m03
        lm[8] = ((long)(x1.s0)) * sy;  // m12
        lm[7] = ((long)(x2.s0)) * lidy;  // m21
        lm[6] = x3.s0;             // m30
        lm[5] = x0.s0 * sy;        // m02
        lm[4] = x1.s0 * lidy;         // m11
        lm[3] = x2.s0;             // m20
        lm[2] = py;             // m01
        lm[1] = x1.s0;             // m10
        lm[0] = x0.s0;             // m00
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    for( int j = TILE_SIZE/2; j >=1; j = j/2 )
    {
        if(lidy < j)
            for( int i = 0; i < 10; i++ )
                lm[i] = lm[i] + m[i][lidy];
        barrier(CLK_LOCAL_MEM_FENCE);
        if(lidy >= j/2&&lidy < j)
            for( int i = 0; i < 10; i++ )
                m[i][lidy-j/2] = lm[i];
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    if(lidy ==0 &&lidx ==0)
    {
        for(int mt = 0; mt < 10; mt++ )
            mom[mt] = (double)lm[mt];

        if(binary)
        {
            double s = 1./255;
            for( int mt = 0; mt < 10; mt++ )
                mom[mt] *= s;
        }

        double xm = x * mom[0], ym = y*mom[0];

        // accumulate moments computed in each tile

        // + m00 ( = m00' )
        dst_m00[wgidy*dst_cols+wgidx]  = mom[0];

        // + m10 ( = m10' + x*m00' )
        dst_m10[wgidy*dst_cols+wgidx]  = mom[1] + xm;

        // + m01 ( = m01' + y*m00' )
        dst_m01[wgidy*dst_cols+wgidx]  = mom[2] + ym;

        // + m20 ( = m20' + 2*x*m10' + x*x*m00' )
        dst_m20[wgidy*dst_cols+wgidx]  = mom[3] + x * (mom[1] * 2 + xm);

        // + m11 ( = m11' + x*m01' + y*m10' + x*y*m00' )
        dst_m11[wgidy*dst_cols+wgidx]   = mom[4] + x * (mom[2] + ym) + y * mom[1];

        // + m02 ( = m02' + 2*y*m01' + y*y*m00' )
        dst_m02[wgidy*dst_cols+wgidx]   = mom[5] + y * (mom[2] * 2 + ym);

        // + m30 ( = m30' + 3*x*m20' + 3*x*x*m10' + x*x*x*m00' )
        dst_m30[wgidy*dst_cols+wgidx]  = mom[6] + x * (3. * mom[3] + x * (3. * mom[1] + xm));

        // + m21 ( = m21' + x*(2*m11' + 2*y*m10' + x*m01' + x*y*m00') + y*m20')
        dst_m21[wgidy*dst_cols+wgidx]  = mom[7] + x * (2 * (mom[4] + y * mom[1]) + x * (mom[2] + ym)) + y * mom[3];

        // + m12 ( = m12' + y*(2*m11' + 2*x*m01' + y*m10' + x*y*m00') + x*m02')
        dst_m12[wgidy*dst_cols+wgidx]  = mom[8] + y * (2 * (mom[4] + x * mom[2]) + y * (mom[1] + xm)) + x * mom[5];

        // + m03 ( = m03' + 3*y*m02' + 3*y*y*m01' + y*y*y*m00' )
        dst_m03[wgidy*dst_cols+wgidx]  = mom[9] + y * (3. * mom[5] + y * (3. * mom[2] + ym));
    }
}
//#endif
//#if defined (DOUBLE_SUPPORT)
__kernel void CvMoments_D5( __global float* src_data, int src_rows, int src_cols, int src_step, int tileSize_width, int tileSize_height,
                            __global double* dst_m00,
                            __global double* dst_m10,
                            __global double* dst_m01,
                            __global double* dst_m20,
                            __global double* dst_m11,
                            __global double* dst_m02,
                            __global double* dst_m30,
                            __global double* dst_m21,
                            __global double* dst_m12,
                            __global double* dst_m03,
                            int dst_cols, int dst_step,
                            int type, int depth, int cn, int coi, int binary, const int TILE_SIZE)
{
    float tmp_coi[4]; // get the coi data
    float4 tmp[64] ;
    int VLEN_F = 4; // vector length of float
    int gidy = get_global_id(0);
    int gidx = get_global_id(1);
    int wgidy = get_group_id(0);
    int wgidx = get_group_id(1);
    int lidy = get_local_id(0);
    int lidx = get_local_id(1);
    int y = wgidy*TILE_SIZE;  // real Y index of pixel
    int x = wgidx*TILE_SIZE;  // real X index of pixel
    int kcn = (cn==2)?2:4;
    int rstep = min(src_step/4, TILE_SIZE);
    tileSize_height = min(TILE_SIZE, src_rows - y);
    tileSize_width = min(TILE_SIZE, src_cols -x);
    if(tileSize_width < TILE_SIZE)
        for(int i = tileSize_width; i < rstep; i++ )
            *((__global float*)src_data+(y+lidy)*src_step/4+x+i) = 0;
    if( coi > 0 )
        for(int i=0; i < tileSize_width; i+=VLEN_F)
        {
            for(int j=0; j<4; j++)
                tmp_coi[j] = *(src_data+(y+lidy)*src_step/4+(x+i+j)*kcn+coi-1);
            tmp[i/VLEN_F] = (float4)(tmp_coi[0],tmp_coi[1],tmp_coi[2],tmp_coi[3]);
        }
    else
        for(int i=0; i < tileSize_width; i+=VLEN_F)
            tmp[i/VLEN_F] = (float4)(*(src_data+(y+lidy)*src_step/4+x+i),*(src_data+(y+lidy)*src_step/4+x+i+1),*(src_data+(y+lidy)*src_step/4+x+i+2),*(src_data+(y+lidy)*src_step/4+x+i+3));
    float4 zero = (float4)(0);
    float4 full = (float4)(255);
    if( binary )
        for(int i=0; i < tileSize_width; i+=4)
            tmp[i/VLEN_F] = (tmp[i/VLEN_F]!=zero)?full:zero;
    double mom[10];
    __local double m[10][128];
    if(lidy == 0)
        for(int i = 0; i < 10; i ++)
            for(int j = 0; j < 128; j ++)
                m[i][j] = 0;
    barrier(CLK_LOCAL_MEM_FENCE);
    double lm[10] = {0};
    double4 x0 = (double4)(0);
    double4 x1 = (double4)(0);
    double4 x2 = (double4)(0);
    double4 x3 = (double4)(0);
    for( int xt = 0 ; xt < tileSize_width; xt+=VLEN_F )
    {
        double4 v_xt = (double4)(xt, xt+1, xt+2, xt+3);
        double4 p = convert_double4(tmp[xt/VLEN_F]);
        double4 xp = v_xt * p, xxp = xp * v_xt;
        x0 += p;
        x1 += xp;
        x2 += xxp;
        x3 += xxp * v_xt;
    }
    x0.s0 += x0.s1 + x0.s2 + x0.s3;
    x1.s0 += x1.s1 + x1.s2 + x1.s3;
    x2.s0 += x2.s1 + x2.s2 + x2.s3;
    x3.s0 += x3.s1 + x3.s2 + x3.s3;
/*
    double py = lidy * x0.s0, sy = lidy*lidy;
    int bheight = min(tileSize_height, TILE_SIZE/2);
    if(bheight >= TILE_SIZE/2&&lidy > bheight-1&&lidy < tileSize_height)
    {
        m[9][lidy-bheight] = ((double)py) * sy;  // m03
        m[8][lidy-bheight] = ((double)x1.s0) * sy;  // m12
        m[7][lidy-bheight] = ((double)x2.s0) * lidy;  // m21
        m[6][lidy-bheight] = x3.s0;             // m30
        m[5][lidy-bheight] = x0.s0 * sy;        // m02
        m[4][lidy-bheight] = x1.s0 * lidy;         // m11
        m[3][lidy-bheight] = x2.s0;             // m20
        m[2][lidy-bheight] = py;             // m01
        m[1][lidy-bheight] = x1.s0;             // m10
        m[0][lidy-bheight] = x0.s0;             // m00
    }
    else if(lidy < bheight)
    {
        lm[9] = ((double)py) * sy;  // m03
        lm[8] = ((double)x1.s0) * sy;  // m12
        lm[7] = ((double)x2.s0) * lidy;  // m21
        lm[6] = x3.s0;             // m30
        lm[5] = x0.s0 * sy;        // m02
        lm[4] = x1.s0 * lidy;         // m11
        lm[3] = x2.s0;             // m20
        lm[2] = py;             // m01
        lm[1] = x1.s0;             // m10
        lm[0] = x0.s0;             // m00
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    for( int j = TILE_SIZE/2; j >= 1; j = j/2 )
    {
        if(lidy < j)
            for( int i = 0; i < 10; i++ )
                lm[i] = lm[i] + m[i][lidy];
        barrier(CLK_LOCAL_MEM_FENCE);
        if(lidy >= j/2&&lidy < j)
            for( int i = 0; i < 10; i++ )
                m[i][lidy-j/2] = lm[i];
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    if(lidy == 0&&lidx == 0)
    {
        for(int mt = 0; mt < 10; mt++ )
            mom[mt] = (double)lm[mt];

        if(binary)
        {
            double s = 1./255;
            for( int mt = 0; mt < 10; mt++ )
                mom[mt] *= s;
        }

        double xm = x * mom[0], ym = y * mom[0];

        // accumulate moments computed in each tile

        // + m00 ( = m00' )
        dst_m00[wgidy*dst_cols+wgidx]= mom[0];

        // + m10 ( = m10' + x*m00' )
        dst_m10[wgidy*dst_cols+wgidx] = mom[1] + xm;

        // + m01 ( = m01' + y*m00' )
        dst_m01[wgidy*dst_cols+wgidx] = mom[2] + ym;

        // + m20 ( = m20' + 2*x*m10' + x*x*m00' )
        dst_m20[wgidy*dst_cols+wgidx] = mom[3] + x * (mom[1] * 2 + xm);

        // + m11 ( = m11' + x*m01' + y*m10' + x*y*m00' )
        dst_m11[wgidy*dst_cols+wgidx] = mom[4] + x * (mom[2] + ym) + y * mom[1];

        // + m02 ( = m02' + 2*y*m01' + y*y*m00' )
        dst_m02[wgidy*dst_cols+wgidx]= mom[5] + y * (mom[2] * 2 + ym);

        // + m30 ( = m30' + 3*x*m20' + 3*x*x*m10' + x*x*x*m00' )
        dst_m30[wgidy*dst_cols+wgidx]= mom[6] + x * (3. * mom[3] + x * (3. * mom[1] + xm));

        // + m21 ( = m21' + x*(2*m11' + 2*y*m10' + x*m01' + x*y*m00') + y*m20')
        dst_m21[wgidy*dst_cols+wgidx] = mom[7] + x * (2 * (mom[4] + y * mom[1]) + x * (mom[2] + ym)) + y * mom[3];

        // + m12 ( = m12' + y*(2*m11' + 2*x*m01' + y*m10' + x*y*m00') + x*m02')
        dst_m12[wgidy*dst_cols+wgidx] = mom[8] + y * (2 * (mom[4] + x * mom[2]) + y * (mom[1] + xm)) + x * mom[5];

        // + m03 ( = m03' + 3*y*m02' + 3*y*y*m01' + y*y*y*m00' )
        dst_m03[wgidy*dst_cols+wgidx]= mom[9] + y * (3. * mom[5] + y * (3. * mom[2] + ym));
    }*/
}
//#endif
//#if defined (DOUBLE_SUPPORT)
__kernel void CvMoments_D6(__global double* src_data,  int src_rows, int src_cols, int src_step, int tileSize_width, int tileSize_height,
                           __global double* dst_m00,
                           __global double* dst_m10,
                           __global double* dst_m01,
                           __global double* dst_m20,
                           __global double* dst_m11,
                           __global double* dst_m02,
                           __global double* dst_m30,
                           __global double* dst_m21,
                           __global double* dst_m12,
                           __global double* dst_m03,
                           int dst_cols, int dst_step,
                           int type, int depth, int cn, int coi, int binary, const int TILE_SIZE)
{
    double tmp_coi[4]; // get the coi data
    double4 tmp[64];
    int VLEN_D = 4; // length of vetor
    int gidy = get_global_id(0);
    int gidx = get_global_id(1);
    int wgidy = get_group_id(0);
    int wgidx = get_group_id(1);
    int lidy = get_local_id(0);
    int lidx = get_local_id(1);
    int y = wgidy*TILE_SIZE;  // real Y index of pixel
    int x = wgidx*TILE_SIZE;  // real X index of pixel
    int kcn = (cn==2)?2:4;
    int rstep = min(src_step/8, TILE_SIZE);
    tileSize_height = min(TILE_SIZE,  src_rows - y);
    tileSize_width = min(TILE_SIZE, src_cols - x);

    if(tileSize_width < TILE_SIZE)
        for(int i = tileSize_width; i < rstep; i++ )
            *((__global double*)src_data+(y+lidy)*src_step/8+x+i) = 0;
    if( coi > 0 )
        for(int i=0; i < tileSize_width; i+=VLEN_D)
        {
            for(int j=0; j<4; j++)
                tmp_coi[j] = *(src_data+(y+lidy)*src_step/8+(x+i+j)*kcn+coi-1);
            tmp[i/VLEN_D] = (double4)(tmp_coi[0],tmp_coi[1],tmp_coi[2],tmp_coi[3]);
        }
    else
        for(int i=0; i < tileSize_width; i+=VLEN_D)
            tmp[i/VLEN_D] = (double4)(*(src_data+(y+lidy)*src_step/8+x+i),*(src_data+(y+lidy)*src_step/8+x+i+1),*(src_data+(y+lidy)*src_step/8+x+i+2),*(src_data+(y+lidy)*src_step/8+x+i+3));
    double4 zero = (double4)(0);
    double4 full = (double4)(255);
    if( binary )
        for(int i=0; i < tileSize_width; i+=VLEN_D)
            tmp[i/VLEN_D] = (tmp[i/VLEN_D]!=zero)?full:zero;
    double mom[10];
    __local double m[10][128];
    if(lidy == 0)
        for(int i=0; i<10; i++)
            for(int j=0; j<128; j++)
                m[i][j]=0;
    barrier(CLK_LOCAL_MEM_FENCE);
    double lm[10] = {0};
    double4 x0 = (double4)(0);
    double4 x1 = (double4)(0);
    double4 x2 = (double4)(0);
    double4 x3 = (double4)(0);
    for( int xt = 0 ; xt < tileSize_width; xt+=VLEN_D )
    {
        double4 v_xt = (double4)(xt, xt+1, xt+2, xt+3);
        double4 p = tmp[xt/VLEN_D];
        double4 xp = v_xt * p, xxp = xp * v_xt;
        x0 += p;
        x1 += xp;
        x2 += xxp;
        x3 += xxp *v_xt;
    }
    x0.s0 += x0.s1 + x0.s2 + x0.s3;
    x1.s0 += x1.s1 + x1.s2 + x1.s3;
    x2.s0 += x2.s1 + x2.s2 + x2.s3;
    x3.s0 += x3.s1 + x3.s2 + x3.s3;

    double py = lidy * x0.s0, sy = lidy*lidy;
    int bheight = min(tileSize_height, TILE_SIZE/2);
    if(bheight >= TILE_SIZE/2&&lidy > bheight-1&&lidy < tileSize_height)
    {
        m[9][lidy-bheight] = ((double)py) * sy;  // m03
        m[8][lidy-bheight] = ((double)x1.s0) * sy;  // m12
        m[7][lidy-bheight] = ((double)x2.s0) * lidy;  // m21
        m[6][lidy-bheight] = x3.s0;             // m30
        m[5][lidy-bheight] = x0.s0 * sy;        // m02
        m[4][lidy-bheight] = x1.s0 * lidy;         // m11
        m[3][lidy-bheight] = x2.s0;             // m20
        m[2][lidy-bheight] = py;             // m01
        m[1][lidy-bheight] = x1.s0;             // m10
        m[0][lidy-bheight] = x0.s0;             // m00
    }

    else if(lidy < bheight)
    {
        lm[9] = ((double)py) * sy;  // m03
        lm[8] = ((double)x1.s0) * sy;  // m12
        lm[7] = ((double)x2.s0) * lidy;  // m21
        lm[6] = x3.s0;             // m30
        lm[5] = x0.s0 * sy;        // m02
        lm[4] = x1.s0 * lidy;         // m11
        lm[3] = x2.s0;             // m20
        lm[2] = py;             // m01
        lm[1] = x1.s0;             // m10
        lm[0] = x0.s0;             // m00
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    for( int j = TILE_SIZE/2; j >= 1; j = j/2 )
    {
        if(lidy < j)
            for( int i = 0; i < 10; i++ )
                lm[i] = lm[i] + m[i][lidy];
        barrier(CLK_LOCAL_MEM_FENCE);
        if(lidy >= j/2&&lidy < j)
            for( int i = 0; i < 10; i++ )
                m[i][lidy-j/2] = lm[i];
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    if(lidy == 0&&lidx == 0)
    {
        for( int mt = 0; mt < 10; mt++ )
            mom[mt] = (double)lm[mt];
        if(binary)
        {
            double s = 1./255;
            for( int mt = 0; mt < 10; mt++ )
                mom[mt] *= s;
        }

        double xm = x * mom[0], ym = y * mom[0];

        // accumulate moments computed in each tile

        // + m00 ( = m00' )
        dst_m00[wgidy*dst_cols+wgidx] = mom[0];

        // + m10 ( = m10' + x*m00' )
        dst_m10[wgidy*dst_cols+wgidx] = mom[1] + xm;

        // + m01 ( = m01' + y*m00' )
        dst_m01[wgidy*dst_cols+wgidx] = mom[2] + ym;

        // + m20 ( = m20' + 2*x*m10' + x*x*m00' )
        dst_m20[wgidy*dst_cols+wgidx]  = mom[3] + x * (mom[1] * 2 + xm);

        // + m11 ( = m11' + x*m01' + y*m10' + x*y*m00' )
        dst_m11[wgidy*dst_cols+wgidx]  = mom[4] + x * (mom[2] + ym) + y * mom[1];

        // + m02 ( = m02' + 2*y*m01' + y*y*m00' )
        dst_m02[wgidy*dst_cols+wgidx]  = mom[5] + y * (mom[2] * 2 + ym);

        // + m30 ( = m30' + 3*x*m20' + 3*x*x*m10' + x*x*x*m00' )
        dst_m30[wgidy*dst_cols+wgidx]  = mom[6] + x * (3. * mom[3] + x * (3. * mom[1] + xm));

        // + m21 ( = m21' + x*(2*m11' + 2*y*m10' + x*m01' + x*y*m00') + y*m20')
        dst_m21[wgidy*dst_cols+wgidx]  = mom[7] + x * (2 * (mom[4] + y * mom[1]) + x * (mom[2] + ym)) + y * mom[3];

        // + m12 ( = m12' + y*(2*m11' + 2*x*m01' + y*m10' + x*y*m00') + x*m02')
        dst_m12[wgidy*dst_cols+wgidx]  = mom[8] + y * (2 * (mom[4] + x * mom[2]) + y * (mom[1] + xm)) + x * mom[5];

        // + m03 ( = m03' + 3*y*m02' + 3*y*y*m01' + y*y*y*m00' )
        dst_m03[wgidy*dst_cols+wgidx]  = mom[9] + y * (3. * mom[5] + y * (3. * mom[2] + ym));
    }
}
//#endif
