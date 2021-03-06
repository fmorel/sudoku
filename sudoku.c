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
    {2, 2, 2},
    {3, 2, 3},
    {3, 2, 5}
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
typedef struct {
    uint16_t values; //Bitmap of possible values
    uint16_t nbValues;
    bool flag;
} Cell;


typedef struct {
    Cell cells[SIZE][SIZE];
    int i;
    int j;
    unsigned int choice;
} Stack;


typedef struct {
    Cell *cells[SIZE];
} Set;


/* Global state */
typedef struct {
    Cell cells[SIZE][SIZE];
    Stack stack[MAX_STACK];
    int stackDepth;

    Set rows[SIZE];
    Set columns[SIZE];
    Set squares[SIZE];

    bool error;
    Args *args;
} State;

/***********************************************************************/
/* Input / output methods */

void gridInput(State *s)
{
    FILE *file = fopen(s->args->filename, "r");
    const uint16_t initialValues = (1<<SIZE)-1;
    int i,j;
    for (i=0; i < SIZE; i++) {
        for (j=0; j < SIZE; j++) {
            char value;
            fscanf(file, "%c ", &value);
            if (value=='x' || value=='0') {
                s->cells[i][j].values = initialValues;
                s->cells[i][j].nbValues = SIZE;
            } else if (value>'0' && value<='9') {
                s->cells[i][j].values = 1<<((value - '0')-1);
                s->cells[i][j].nbValues = 1;
            } else {
                printf("Unrecognized character at position (%d,%d) : %c\n", i, j, value);
            }
        }
        fscanf(file, "\n");
    }
    fclose(file);
}

void gridOutput(State *s)
{
    int i,j;
    for (i=0; i < SIZE; i++) {
        for (j=0; j < SIZE; j++) {
            if (s->cells[i][j].nbValues==1) {
                int value = __builtin_ffs(s->cells[i][j].values);
                printf("%4d ", value);
            } else {
                printf("x(%1d) ", s->cells[i][j].nbValues);
            }
        }
        printf("\n");
    }
}



/***********************************************************************/
/* Utilities to handle set of values */

bool removeValueFromCell(State *s, Cell *cell, uint16_t valueBmp)
{
    int i;
    uint16_t prev=cell->values;
    cell->values &= ~(valueBmp);
    uint16_t diff = prev ^ cell->values;
    cell->nbValues -= __builtin_popcount(diff);
    if (cell->nbValues <= 0)
        s->error = true;	
    return (diff!=0);
}


bool isEqual(Cell *a, Cell *b)
{
    return (a->values == b->values);
}

bool isSolved(State *s)
{
    int i,j;
    bool solved = true;

    for (i=0; i < SIZE; i++) {
        for (j=0; j < SIZE; j++) {
            solved = solved && (s->cells[i][j].nbValues == 1);
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

bool analyseSet(State *s, Set *set, int nbElem)
{
    Cell *candidate;
    Cell *subset[SIZE];
    int i,j, matching, n = 0;
    bool efficient = false;
    int nextCandidate = 0;

    //Build subset of potential cells
    for (i = 0; i < SIZE; i++) {
        if (set->cells[i]->nbValues == nbElem)
            subset[n++] = set->cells[i];
    }

    while ((n - nextCandidate) >= nbElem) {
        candidate = subset[nextCandidate++];
        candidate->flag = true;
        matching = 1;

        for (i = nextCandidate; i < n; i++) {
            //Check if the values are same as candidate
            if (isEqual(subset[i], candidate)) {
                subset[i]->flag = true;
                matching++;
            }
            //We've found something here, leave and prune :
            if (matching == nbElem) {
                break;
            }
        }

        if (matching == nbElem) {
            for (i = 0; i < SIZE; i++) {
                if (set->cells[i]->flag)
                    continue;
                //Prune other cells
                bool effect = removeValueFromCell(s, set->cells[i], candidate->values);
                efficient = effect || efficient;
            }
        } 

        //Reset flag
        for (i = nextCandidate-1; i < n; i++) {
            subset[i]->flag = false;
        }
    }

    return efficient;
}

/* Make sure that if a value can only be in one place, then it's here (existence principle) */
bool existenceSet(State *s, Set *set) {
    int i,j,k;
    int bmp;
    Cell *candidate;
    bool efficient = false;

    // This seems inefficient : deactivate
    return false;

    for (i = 0; i < SIZE; i++) {
        bmp = (1<<i);
        k = 0;
        for (j = 0; j < SIZE; j++) {
            if (bmp & set->cells[j]->values == bmp) {
                candidate = set->cells[j];
                k++;
            }
            if (k > 1)
                break;
        }
        if (k == 1) {
            efficient = efficient || (candidate->nbValues > 1);
            candidate->values = bmp;
            candidate->nbValues = 1;
        }
    }

    return efficient;
}
/***********************************************************************/
/* Hypothesis related code */

/* For now, a path is taken when a cell alternates between 2 possiblities (nearly always the case after a first pass)*/
void runHypothesis(State *s)
{
    int i,j;
    memcpy(&s->stack[s->stackDepth].cells, s->cells, sizeof(s->cells));
    for (i=0; i < SIZE; i++) {
        for (j=0; j < SIZE; j++) {
            Cell *c = &s->cells[i][j];
            if (c->nbValues == 2) {
                //Will clear the lowest bit
                c->values &= c->values - 1;
                c->nbValues = 1;
                //Remember this choice in stack
                s->stack[s->stackDepth].i = i;
                s->stack[s->stackDepth].j = j;
                s->stack[s->stackDepth++].choice = c->values;
                if (s->args->verbose)
                    printf("Hypothesis : (%d,%d) takes value 0x%x (%d)\n", i, j, s->cells[i][j].values, s->cells[i][j].nbValues);
                return;
            }
        }
    }
    printf("No more hypothesis ...\n");
}

/* If the hypothesis proves wrong, choose the alternative option */

void revertHypothesis(State *s)
{
    s->stackDepth--;
    memcpy(s->cells, &s->stack[s->stackDepth].cells, sizeof(s->cells));
    int i = s->stack[s->stackDepth].i;
    int j = s->stack[s->stackDepth].j;
    unsigned int choice = s->stack[s->stackDepth].choice;
    if (s->args->verbose)
        printf("Hypothesis is wrong, take other path ...\n");
    removeValueFromCell(s, &s->cells[i][j], choice);
}


/***********************************************************************/
/* Extraction of the base subsets of a sudoku (row, column and square) */
void extractRow(State *s, Set *set, int idx)
{
    int i;
    for (i=0; i < SIZE; i++) {
        set->cells[i] = &s->cells[idx][i];
    }
}

void extractColumn(State *s, Set *set, int idx)
{
    int i;
    for (i=0; i < SIZE; i++) {
        set->cells[i] = &s->cells[i][idx];
    }
}

void extractSquare(State *s, Set *set, int idx)
{
    int i,j,k=0;
    int baseRow=(idx/SMALL_SIZE)*SMALL_SIZE;
    int baseColumn=(idx%SMALL_SIZE)*SMALL_SIZE;
    for (i=0; i < SMALL_SIZE; i++) {
        for (j=0; j < SMALL_SIZE;j++) {
            set->cells[k] = &s->cells[baseRow+i][baseColumn+j];
            k++;
        }
    }
}

void prepareSets(State *s)
{
    int i;
    for (i=0; i < SIZE; i++) {
        extractRow(s, &s->rows[i], i);
        extractColumn(s, &s->columns[i], i);
        extractSquare(s, &s->squares[i], i);
    }
}

/* Solver itself */
int solve(Args *args)
{
    bool solved;
    const DifficultySettings *setting = &settings[args->level];
    State *s = calloc(1, sizeof(State));

    s->args = args;
    gridInput(s);
    if (args->verbose) {
        printf("Input grid is \n");
        gridOutput(s);
        printf("Difficulty level is %d\n", args->level);
    }

    clock_t start = clock();

    prepareSets(s);

    //Try to solve the problem after each hypothesis (or none if Sudoku is simple)
    while(s->stackDepth < setting->maxStack) {
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
                        effect = analyseSet(s, &s->rows[i], force);	
                        effect = existenceSet(s, &s->rows[i]) || effect;
                        efficient = efficient || effect;

                        effect = analyseSet(s, &s->columns[i], force);	
                        effect = existenceSet(s, &s->columns[i]) || effect;
                        efficient = efficient || effect;

                        effect = analyseSet(s, &s->squares[i], force);	
                        effect = existenceSet(s, &s->squares[i]) || effect;
                        efficient = efficient || effect;
                    }
                    solved = isSolved(s);
                } while (efficient && !solved && !s->error);
                force++;
            } while (force < setting->force && !solved && !s->error);
        } while (iter < setting->maxIter && !solved && !s->error); 

        if (solved) {
            clock_t stop = clock();
            unsigned long duration = (1000000*(stop-start))/CLOCKS_PER_SEC;
            printf("Sudoku solved in %lu us: \n", duration);
            gridOutput(s);
            break;
        } else if (!s->error && args->verbose) {
            printf("Pass %d is not sufficient, need hypothesis ...\nCurrent state is:\n", s->stackDepth+1);
            gridOutput(s);
        }

        //Hypothesis
        if (s->error) {
            revertHypothesis(s);
            s->error = false;
        } else {
            runHypothesis(s);
        }
    }

    if (!solved) {
        printf("Solver is not strong enough :(\nTry to increase the level of the solver (was %d)\n", args->level);
        gridOutput(s);
    }
    
    free(s);
    return 0;
}

/* Grid generation */
void gridGenerateBase(State *s)
{
    int i, j, value;
    /* Generate a base valid grid */
    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            value = j + SMALL_SIZE*(i%SMALL_SIZE) + i/SMALL_SIZE;
            value %= SIZE;
            s->cells[i][j].values = (1 << value);
            s->cells[i][j].nbValues = 1;
        }
    }
}

void gridPermuteDigits(State *s, int a, int b)
{
    int i, j; 
    
    if (a == b)
        return;
    
    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            if (s->cells[i][j].values == (1 << a))
                s->cells[i][j].values = (1 << b);
            else if (s->cells[i][j].values == (1 << b))
                s->cells[i][j].values = (1 << a);
        }
    }
}

void gridPermuteRows(State *s, int i, int j)
{
    Cell row[SIZE];
    if (i == j)
        return;
    memcpy(row, s->cells[i], sizeof(row));
    memcpy(s->cells[i], s->cells[j], sizeof(row));
    memcpy(s->cells[j], row, sizeof(row));
}

void gridPermuteRowBlocks(State *s, int i, int j)
{
    Cell block[SMALL_SIZE][SIZE];

    if (i == j)
        return;
    memcpy(block, &s->cells[i*3], sizeof(block));
    memcpy(&s->cells[i*3], &s->cells[j*3], sizeof(block));
    memcpy(&s->cells[j*3], block, sizeof(block));
}

void gridRotate(State *s)
{
    int i,j;
    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            s->stack[0].cells[i][j] = s->cells[j][i];
        }
    }
    memcpy(s->cells, s->stack[0].cells, sizeof(s->cells));
}

int generate(Args *args)
{
    const DifficultySettings *setting = &settings[args->level];
    int iter, r, i, j, k;
    
    State *s = calloc(1, sizeof(State));
    s->args = args;
    
    srand(time(NULL)); // randomize seed
    
    gridGenerateBase(s);
    for (iter = 0; iter < 500; iter++) {
        r = rand() % 100;
        if (r < 15) {
            i = rand() % SIZE;
            j = rand() % SIZE;
            gridPermuteDigits(s, i, j);
        } else if (r < 45) {
            i = rand() % SMALL_SIZE;
            j = rand() % SMALL_SIZE;
            k = rand() % SMALL_SIZE;
            i += 3*k;
            j += 3*k;
            gridPermuteRows(s, i, j);
        } else if (r < 75) {
            i = rand() % SMALL_SIZE;
            j = rand() % SMALL_SIZE;
            gridPermuteRowBlocks(s, i, j);
        } else {
            gridRotate(s);
        }
    }
    printf("Generated grid : \n");
    gridOutput(s);

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

int main (int argc, char **argv)
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
        return generate(&args);
    } 

    //Should not happen
    return -1;
}



