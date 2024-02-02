//
//  solve.h
//  MakeHDR
//
//  Created by Vahan Sosoyan 2024.
//

#include <vector>
#include <string>

extern "C"
{
#include "f2c.h"
#include "clapack.h"
}


namespace lapack
{
    doublereal rcond_trimat(doublereal *A, integer n, integer layout)
    {
        char norm_id = '1';
        char uplo = (layout == 0) ? 'U' : 'L';
        char diag = 'N';
        doublereal rcond = doublereal(0);
        integer info = integer(0);
        
        doublereal* work = new doublereal[3*n];
        integer* iwork = new integer[n];
        
        dtrcon_(&norm_id, &uplo, &diag, &n, A, &n, &rcond, work, iwork, &info);
        
        if(info != integer(0))  { return doublereal(0); }
        
        delete[] work;
        delete[] iwork;

        return rcond;
    }
    
    doublereal rect_rcond(doublereal *_out, doublereal *A, doublereal *B, integer m, integer n)
    {
        doublereal* tmp_a = new doublereal[m*n];
        for (int i=0; i<m*n; ++i)
            tmp_a[i] = A[i];
        
        doublereal* tmp_b = new doublereal[m];
        for (int i=0; i<m; ++i)
            tmp_b[i] = B[i];
        
        char trans = 'N';
        integer nrhs = integer(1);
        integer lda = m;
        integer ldb = m;
        integer info = integer(0);
        integer min_mn = min(m, n);
        integer lwork_min = max(integer(1), min_mn + max(min_mn, nrhs));
        
        integer lwork_proposed = 0;
        if((m*n) >= 1024)
        {
            doublereal work_query[2];
            integer lwork_query = integer(-1);
            
            dgels_(&trans, &m, &n, &nrhs, tmp_a, &lda, tmp_b, &ldb, &work_query[0], &lwork_query, &info);
            
            lwork_proposed = static_cast<integer>(work_query[0]);
        }
        
        integer lwork_final = max(lwork_proposed, lwork_min);
        doublereal* work = new doublereal[lwork_final];
        
        dgels_(&trans, &m, &n, &nrhs, tmp_a, &lda, tmp_b, &ldb, work, &lwork_final, &info);

        std::vector<std::vector<double>> R(n, std::vector<double>(n, 0));
        std::vector<std::vector<double>> _A(m, std::vector<double>(n, 0));
        
        for (int i=0; i<n; ++i)
            for (int j=0; j<m; ++j)
                _A[j][i] = tmp_a[i * m + j];

        for(integer col=0; col < n; ++col)
            for(integer row=0; row <= col; ++row)
                R[row][col] = _A[row][col];
        
        doublereal* _R = new doublereal[m*m];
        
        for (int i=0; i<n; ++i)
            for (int j=0; j<n; ++j)
                _R[i * n + j] = R[j][i];
        
        doublereal rcond = rcond_trimat(_R, n, 0);
        
        if (rcond > 0)
            for (integer i=0; i<n; ++i)
                _out[i] = tmp_b[i];
        
        delete[] tmp_a;
        delete[] tmp_b;
        delete[] work;
        delete[] _R;
        
        return rcond;
    }
    
    bool approx_svd(doublereal *_out, doublereal *A, doublereal *B, integer m, integer n)
    {
        integer min_mn = min(m, n);
        integer nrhs = integer(1);
        integer lda = m;
        integer ldb = m;
        doublereal rcond = integer(-1);
        integer rank = integer(0);
        integer info = integer(0);
        
        doublereal* S = new doublereal[min_mn];
        
        integer ispec = integer(9);
        
        const char* const_name = "DGELSD";
        const char* const_opts = " ";
        
        char* name = const_cast<char*>(const_name);
        char* opts = const_cast<char*>(const_opts);
        
        integer n1 = m;
        integer n2 = n;
        integer n3 = nrhs;
        integer n4 = lda;
        
        integer laenv_result = ilaenv_(&ispec, name, opts, &n1, &n2, &n3, &n4);
        
        integer smlsiz = max(integer(25), laenv_result );
        integer smlsiz_p1 = integer(1) + smlsiz;
        
        integer nlvl   = max(integer(0), integer(1) + integer(log(doublereal(min_mn) / doublereal(smlsiz_p1)) / doublereal(0.69314718055994530942)));
        integer liwork = max(integer(1), (integer(3) * min_mn * nlvl + integer(11)*min_mn));
        
        integer* iwork = new integer[liwork];
        
        integer lwork_min = integer(12)*min_mn + integer(2)*min_mn*smlsiz + integer(8)*min_mn*nlvl + min_mn*nrhs + smlsiz_p1*smlsiz_p1;
        
        doublereal work_query[2];
        integer lwork_query = integer(-1);
        
        dgelsd_(&m, &n, &nrhs, A, &lda, B, &ldb, S, &rcond, &rank, &work_query[0], &lwork_query, iwork, &info);
        
        integer lwork_proposed = static_cast<integer>(work_query[0]);
        integer lwork_final = max(lwork_proposed, lwork_min);
        
        doublereal* work = new doublereal[lwork_final];
        
        dgelsd_(&m, &n, &nrhs, A, &lda, B, &ldb, S, &rcond, &rank, work, &lwork_final, iwork, &info);
        
        delete[] S;
        delete[] work;
        delete[] iwork;
        
        if (info == 0)
            for (integer i=0; i<n; ++i)
                _out[i] = B[i];
        else
            return false;
        
        return true;
    }
    
    bool solve(doublereal *_out, doublereal *A, doublereal *B, integer m, integer n)
    {
        doublereal rcond = 0;
        
        rcond = rect_rcond(_out, A, B, m, n);

        if (rcond == 0)
            return approx_svd(_out, A, B, m, n);
        
        return true;
    }
}
