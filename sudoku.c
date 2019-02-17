#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <argp.h>

//Sudoku size. SIZE = SMALL_SIZE*SMALL_SIZE
#define SIZE 9
#define SMALL_SIZE 3

//Increasing these parameters increase the power of the solver up to a certain point and at a certain computation cost
#define FORCE 3
#define MAX_ITER 3
#define MAX_STACK 5

typedef enum {
    DIFF_EASY,
    DIFF_MEDIUM,
    DIFF_HARD,
    DIFF_EXPERT,
    DIFF_QTY
} DifficultyLevel;

typedef struct {
    int force;
    int maxIter;
    int maxStack;
} DifficultySettings;

DifficultySettings settings[DIFF_QTY] = 
{
    {1, 4, 1},
    {2, 3, 2},
    {3, 3, 3},
    {3, 3, 5}
};

typedef struct {
    char *filename;
    bool verbose;
    bool solve;
    bool generate;
    int level;
} Args;

/***********************************************************************/
/* Data types */
typedef struct CellTag {
	uint16_t values; //Bitmap of possible values
	uint16_t nbValues;
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
	const uint16_t initialValues = (1<<SIZE)-1;
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
				printf("Unrecognized character at position (%d,%d) : %c\n", i, j, value);
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
				int value = __builtin_ffs(cells[i][j].values);
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

bool removeValueFromCell(Cell *cell, uint16_t valueBmp)
{
	int i;
	uint16_t prev=cell->values;
	cell->values &= ~(valueBmp);
	uint16_t diff = prev ^ cell->values;
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
                                //Will clear the lowest bit
                                cells[i][j].values &= cells[i][j].values - 1;
                                cells[i][j].nbValues = 1;
				//Remember this choice in stack
				stack[stackDepth].i = i;
				stack[stackDepth].j = j;
				stack[stackDepth].choice = cells[i][j].values;
				stackDepth++;
				if (debug)
					printf("Hypothesis : (%d,%d) takes value 0x%x (%d)\n", i, j, cells[i][j].values, cells[i][j].nbValues);
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

/* Solver itself */
int solve(Args *args)
{
	bool solved;
        const DifficultySettings *setting = &settings[args->level];
	
	gridInput(args->filename);
	if (args->verbose) {
            printf("Input grid is \n");
	    gridOutput();
            printf("Difficulty level is %d\n", args->level);
        }

	clock_t start = clock();
	
	prepareSets();

	//Try to solve the problem after each hypothesis (or none if Sudoku is simple)
	while(stackDepth < setting->maxStack) {
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
			} while (force < setting->force && !solved && !error);
		} while (iter < setting->maxIter && !solved && !error); 
		
		if (solved) {
			clock_t stop = clock();
			unsigned long duration = (1000000*(stop-start))/CLOCKS_PER_SEC;
                        printf("Sudoku solved in %lu us: \n", duration);
                        gridOutput();
			break;
		} else if (!error && args->verbose) {
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
		printf("Solver is not strong enough :(\nTry to increase the level of the solver (was %d)\n", args->level);
	}

	return 0;
}

/* Argument handling */
static char doc[] =
  "Sudoku solver";

/* The options we understand. */
static struct argp_option options[] = {
  {"solve",     's', "FILE",  0,  "Solve sudoku problem contained in FILE. Format is space separated digits or 'x' for blank cell.\n"
                                   "Solution is printed on stdout. Cannot be used if 'generate' is selected." },
  {"level",     'l', "LEVEL", 0,  "Limit sudoku solver/generator level (0: easy, 1 :medium, 2:hard, 3: expert). Default is 1"},
  {"generate",  'g', "FILE",  0,  "Generate sudoku and store it in FILE. Cannot be used if 'solve' is selcted."},
  {"verbose",   'v', 0,       0,  "Produce verbose information during sudoku solving." },
  { 0 }
};

/* Used by main to communicate with parse_opt. */


/* Parse a single option. */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
    Args *args = state->input;

    switch (key)
    {
        case 's':
            if (args->generate)
                argp_usage(state);
            args->solve = true;
            args->filename = arg;
            break;
        case 'l':
            args->level = atoi(arg);
            if (args->level < 0 || args->level >= DIFF_QTY)
                argp_usage(state);
            break;
        case 'g':
            if (args->solve)
                argp_usage(state);
            args->generate = true;
            args->filename = arg;
            break;
        case 'v':
            args->verbose = true;
            break;
        case ARGP_KEY_END:
            if (!args->generate && !args->solve)
                argp_usage(state);
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

int
main (int argc, char **argv)
{
    Args args;
    struct argp argp = {options, parse_opt, NULL, doc, NULL, NULL, NULL};

    memset(&args, 0, sizeof(Args));
    args.level = 1;

    /* Parse our args; every option seen by parse_opt will
       be reflected in args. */
    argp_parse (&argp, argc, argv, 0, 0, &args);
    
    if (args.solve) {
        return solve(&args);
    } 
    if (args.generate) {
        printf("Sudoku generation not developped yet\n");
        return 0;
    } 

    //Should not happen
    return -1;
}



