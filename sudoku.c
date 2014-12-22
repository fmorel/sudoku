#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

//Sudoku size. SIZE = SMALL_SIZE*SMALL_SIZE
#define SIZE 9
#define SMALL_SIZE 3

//Increasing these parameters increase the power of the solver up to a certain point and at a certain computation cost
#define FORCE 3
#define MAX_ITER 3
#define MAX_STACK 5


/***********************************************************************/
/* Data types */
typedef struct CellTag {
	unsigned int values; //Bitmap of possible values
	unsigned int nbValues;
} Cell;


typedef struct StackTag {
	Cell cells[SIZE][SIZE];
	int i;
	int j;
	unsigned int choice;
} Stack;


typedef struct SetTag {
	Cell *cells[SIZE];
	int nbCells;
} Set;


/* Global variables */

Cell cells[SIZE][SIZE];

Stack stack[MAX_STACK];
int stackDepth = 0;

Set rows[SIZE];
Set columns[SIZE];
Set squares[SIZE];

bool error = false;
bool debug = false;
bool timeOnly = true;

/***********************************************************************/
/* Input / output methods */

void gridInput(char *filename)
{
	FILE *file = fopen(filename, "r");
	const unsigned int initialValues = (1<<SIZE)-1;
	int i,j;
	for (i=0; i < SIZE; i++) {
		for (j=0; j < SIZE; j++) {
			char value;
			fscanf(file, "%c ", &value);
			if (value=='x' || value=='0') {
				cells[i][j].values = initialValues;
				cells[i][j].nbValues = SIZE;
			} else if (value>'0' && value<='9') {
				cells[i][j].values = 1<<((value - '0')-1);
				cells[i][j].nbValues = 1;
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
				unsigned int bitmap = cells[i][j].values;
				int value=0;
				while (bitmap) {
					value++;
					bitmap>>=1;
				}
				printf("%4d ", value);
			} else {
				printf("x(%1d) ", cells[i][j].nbValues);
			}
		}
		printf("\n");
	}
}


/***********************************************************************/
/* Utilities to handle set of values */

bool removeValueFromCell(Cell *cell, unsigned int valueBmp)
{
	int i;
	unsigned int prev=cell->values;
	cell->values &= ~(valueBmp);
	unsigned int diff = prev ^ cell->values;
	cell->nbValues -= __builtin_popcount(diff);
	if (cell->nbValues <= 0)
		error = true;	
	return (diff!=0);
}


bool isEqual(Cell *a, Cell *b)
{
	return (a->values == b->values);
}

bool isSolved(void)
{
	int i,j;
	bool solved = true;
	
	for (i=0; i < SIZE; i++) {
		for (j=0; j < SIZE; j++) {
			solved = solved && (cells[i][j].nbValues == 1);
			if(!solved)
				break;
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
				bool effect = removeValueFromCell(set->cells[i], candidates[0]->values);
				efficient = effect || efficient;
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
				//Found first value and remove it
				int order = SIZE-1;
				while (!((1<<order) & (cells[i][j].values)))
					order--;
				removeValueFromCell(&cells[i][j], 1<<order);
				//Remember this choice in stack
				stack[stackDepth].i = i;
				stack[stackDepth].j = j;
				stack[stackDepth].choice = cells[i][j].values;
				stackDepth++;
				if (debug)
					printf("Hypothesis : (%d,%d) takes value 0x%x (%d)\n", i,j,cells[i][j].values, cells[i][j].nbValues);
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
	unsigned int choice = stack[stackDepth].choice;
	if (debug)
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

void prepareSets(void)
{
	int i;
	for (i=0; i < SIZE; i++) {
		extractRow(&rows[i], i);
		extractColumn(&columns[i], i);
		extractSquare(&squares[i], i);
	}
}

/***********************************************************************/
/* Main functions handling the different passes */

int main(int argc, char **argv)
{
	bool solved;
	
	gridInput(argv[1]);
	if(!timeOnly)
		gridOutput();

	clock_t start = clock();
	
	prepareSets();

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
						effect = analyseSet(&rows[i], force);	
						efficient = efficient || effect;

						effect = analyseSet(&columns[i], force);	
						efficient = efficient || effect;

						effect = analyseSet(&squares[i], force);	
						efficient = efficient || effect;

						solved = isSolved();
					}
				} while (efficient && !solved && !error);
				force++;
			} while (force<FORCE && !solved && !error);
		} while (iter < MAX_ITER && !solved && !error); 
		
		if (solved) {
			clock_t stop = clock();
			unsigned long duration = (1000000*(stop-start))/CLOCKS_PER_SEC;
			if (timeOnly) {
				printf("%lu\n", duration);
			} else {
				printf("Sudoku solved in %lu us: \n", duration);
				gridOutput();
			}
			break;
		} else if (!error && debug) {
			printf("Pass %d is not sufficient, need hypothesis ...\nCurrent state is:\n", stackDepth+1);
			gridOutput();
		}

		//Hypothesis
		if (error) {
			revertHypothesis();
			error = false;
		} else {
			runHypothesis();
		}
	}

	if (!solved) {
		printf("Solver is not strong enough :(\nTry to increase the FORCE, MAX_ITER or MAX_SIZE value!\n");
	}

	return 0;
}

