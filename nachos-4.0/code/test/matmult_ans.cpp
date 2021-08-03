#include <iostream>

using namespace std;

#define Dim 	20 

int A[Dim][Dim];
int B[Dim][Dim];
int C[Dim][Dim];

int main()
{
    int i, j, k;

    for (i = 0; i < Dim; i++)		/* first initialize the matrices */
	for (j = 0; j < Dim; j++) {
	     A[i][j] = i;
	     B[i][j] = j;
	     C[i][j] = 0;
	}

    for (i = 0; i < Dim; i++)		/* then multiply them together */
	for (j = 0; j < Dim; j++)
            for (k = 0; k < Dim; k++)
		 C[i][j] += A[i][k] * B[k][j];

    cout<<C[Dim-1][Dim-1]<<endl;		/* and then we're done */
}
