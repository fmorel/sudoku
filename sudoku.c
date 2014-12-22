#include <stdio.h>
#include <string.h>
#include <stdbool.h>

//Sudoku size. SIZE = SMALL_SIZE*SMALL_SIZE
#define SIZE 9
#define SMALL_SIZE 3

//Increasing these parameters increase the power of the solver up to a certain point and at a certain computation cost
#define FORCE 3
#define MAX_ITER 3
#define MAX_STACK 2


/***********************************************************************/
/* Data types */
typedef struct CellTag {
	int values[SIZE];
	int nbValues;
} Cell;


typedef struct StackTag {
	Cell cells[SIZE][SIZE];
	int i;
	int j;
	int choice;
} Stack;


typedef struct SetTag {
	Cell *cells[SIZE];
	int nbCells;
} Set;


/* Global variables */

Cell cells[SIZE][SIZE];

Stack stack[MAX_STACK];
int stackDepth = 0;
bool error = false;

/***********************************************************************/
/* Input / output methods */

void gridInput(char *filename)
{
	FILE *file = fopen(filename, "r");
	const int initialValues[SIZE] = {1,2,3,4,5,6,7,8,9};
	int i,j;
	for (i=0; i < SIZE; i++) {
		for (j=0; j < SIZE; j++) {
			char value;
			fscanf(file, "%c ", &value);
			if (value=='x' || value=='0') {
				memcpy(cells[i][j].values, initialValues, sizeof(initialValues));
				cells[i][j].nbValues = SIZE;
			} else if (value>'0' && value<='9') {
				cells[i][j].values[0] = value - '0';
				cells[i][j].nbValues =1;
			} else {
				printf("Unrecognized character at psoition (%d,%d) : %c\n", i, j, value);
			}
		}
		fscanf(file, "\n");
	}
	fclose(file);
}

void gridOutput(void)
{
	int i,j;
	for (i=0; i < SIZE; i++) {
		for (j=0; j < SIZE; j++) {
			if (cells[i][j].nbValues==1) {
				printf("%4d ", cells[i][j].values[0]);
			} else {
				printf("x(%1d) ", cells[i][j].nbValues);
			}
		}
		printf("\n");
	}
}


/***********************************************************************/
/* Utilities to handle set of values */

bool removeValueFromCell(Cell *cell, int value)
{
	int i;
	for (i=0; i<cell->nbValues; i++) {
		if (cell->values[i] == value) {
			memmove(&(cell->values[i]), &(cell->values[i+1]), (cell->nbValues-i-1)*sizeof(cell->values[0]));
			cell->nbValues--;
			if (!cell->nbValues) {
				error=true;
			}
			return true;
		}
	}
	return false;
}

bool isSubset(Cell *a, Cell *b)
{
	int i,j;
	bool ok = true;
	bool found;
	for (i=0; i < a->nbValues; i++) {
		found = false;
		for (j=0; j < b->nbValues; j++) {
			if (a->values[i] == b->values[j]) {
				found = true;
				break;
			}
		}
		if (!found) {
			ok =false;
			break;
		}
	}

	return ok;
}

bool isEqual(Cell *a, Cell *b)
{
	return (isSubset(a,b) && isSubset(b,a));
}

bool isSolved(void)
{
	int i,j;
	bool solved = true;
	
	for (i=0; i < SIZE; i++) {
		for (j=0; j < SIZE; j++) {
			solved = solved && (cells[i][j].nbValues == 1);
		}
	}
	return solved;
}

/***********************************************************************/

/* Core analysis function */

/* If a set of N value is exactly contained in N cells, these N values shall be removed from all other cells from the set */
/* When N=1, it is the basic exclusion principle when a cell already contains a unique value */

bool analyseSet(Set *set, int nbElem)
{
	Cell *candidates[SIZE];
	int i,j, matching;
	bool efficient = false;
	int firstCandidate = -1;

	do {
		matching=0;
		for (i=firstCandidate+1; i<set->nbCells; i++) {
			if (set->cells[i]->nbValues == nbElem) {
				if (matching) {
					//Check if the values are same as candidate
					if (isEqual(set->cells[i], candidates[0])) {
						candidates[matching] = set->cells[i];
						matching++;
					}
				} else {
					//New candidate
					candidates[0] = set->cells[i];
					matching= 1;
					firstCandidate = i;
				}
				//We've found something here, leave and prune :
				if (matching == nbElem) {
					break;
				}
			}
		}

		if (matching==nbElem) {
			for (i=0; i < set->nbCells; i++) {
				//Check it is not a candidate
				bool isCandidate = false;
				for (j=0; j<matching; j++) {
					if (set->cells[i] == candidates[j]) {
						isCandidate = true;
						break;
					}
				}
				if (isCandidate)
					continue;
				//Prune other cells
				for (j=0; j<candidates[0]->nbValues; j++) {
					bool effect = removeValueFromCell(set->cells[i], candidates[0]->values[j]);
					efficient = efficient || effect;
				}
			}
		} 
	} while (matching);

	return efficient;
}

/***********************************************************************/
/* Hypothesis related code */

/* For now, a path is taken when a cell alternates between 2 possiblities (nearly always the case after a first pass)*/
void runHypothesis(void)
{
	int i,j;
	memcpy(&stack[stackDepth].cells, cells, sizeof(cells));
	for (i=0; i < SIZE; i++) {
		for (j=0; j < SIZE; j++) {
			if (cells[i][j].nbValues == 2) {
				cells[i][j].nbValues=1;
				stack[stackDepth].i = i;
				stack[stackDepth].j = j;
				stack[stackDepth].choice = cells[i][j].values[0];
				stackDepth++;
				printf("Hypothesis : (%d,%d) takes value %d\n", i,j,cells[i][j].values[0]);
				return;
			}
		}
	}
	printf("No more hypothesis ...\n");
}

/* If the hypothesis proves wrong, choose the alternative option */

void revertHypothesis(void)
{
	stackDepth--;
	memcpy(cells, &stack[stackDepth].cells, sizeof(cells));
	int i = stack[stackDepth].i;
	int j = stack[stackDepth].j;
	int choice = stack[stackDepth].choice;

	printf("Hypothesis is wrong, take other path ...\n");
	removeValueFromCell(&cells[i][j], choice);
}


/***********************************************************************/
/* Extraction of the base subsets of a sudoku (row, column and square) */

void extractRow(Set *set, int idx)
{
	int i;
	for (i=0; i < SIZE; i++) {
		set->cells[i] = &cells[idx][i];
	}
	set->nbCells=SIZE;
}

void extractColumn(Set *set, int idx)
{
	int i;
	for (i=0; i < SIZE; i++) {
		set->cells[i] = &cells[i][idx];
	}
	set->nbCells=SIZE;
}

void extractSquare(Set *set, int idx)
{
	int i,j,k=0;
	int baseRow=(idx/SMALL_SIZE)*SMALL_SIZE;
	int baseColumn=(idx%SMALL_SIZE)*SMALL_SIZE;
	for (i=0; i < SMALL_SIZE; i++) {
		for (j=0; j < SMALL_SIZE;j++) {
			set->cells[k] = &cells[baseRow+i][baseColumn+j];
			k++;
		}
	}
	set->nbCells = SIZE;
}


/***********************************************************************/
/* Main functions handling the different passes */

int main(int argc, char **argv)
{
	gridInput(argv[1]);
	gridOutput();

	Set currentSet;
	bool solved;

	//Try to solve the problem after each hypothesis (or none if Sudoku is simple)
	while(stackDepth < MAX_STACK) {
		int iter=0;
		//Base iterations
		do {
			int force=1;
			iter++;
			//Increase 'force', i.e. number of elements we are trying to simplify in a subset
			do {
				bool efficient;
				//Iterate base operations until nothing moves
				do {
					efficient = false;
					bool effect;
					int i;
					//Elementary passes over the indexed subset
					for (i=0; i < SIZE; i++) {
						extractRow(&currentSet, i);
						effect = analyseSet(&currentSet, force);	
						efficient = efficient || effect;

						extractColumn(&currentSet, i);
						effect = analyseSet(&currentSet, force);	
						efficient = efficient || effect;

						extractSquare(&currentSet, i);
						effect = analyseSet(&currentSet, force);	
						efficient = efficient || effect;

						solved = isSolved();
					}
				} while (efficient && !solved && !error);
				force++;
			} while (force<FORCE && !solved && !error);
		} while (iter < MAX_ITER && !solved && !error); 
		
		if (solved) {
			printf("Sudoku solved : \n");
			gridOutput();
			break;
		} else {
			printf("Pass %d is not sufficient, need hypothesis ...\nCurrent state is:\n", stackDepth+1);
			gridOutput();
		}

		//Hypothesis
		if (error)
			revertHypothesis();
		error = false;

		runHypothesis();
	}

	return 0;
}

