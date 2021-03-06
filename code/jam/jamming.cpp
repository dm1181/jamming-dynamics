
#define NDIM 3
#define PI 3.14159265
#define PI2 6.28318531
#define sqrt2 1.41421356
#define sqrt3 1.73205081

#include <vector>
#include <cmath>
#include <ctime>
#include <chrono>
#include <time.h>
#include <iostream>

using namespace std;
using namespace std::chrono;

#include "../classes/Cell.h"
#include "../classes/Box.h"
#include "../classes/Print.h"
#include "../classes/Fluctuations.h"
#include "../classes/Correlations.h"
#include <boost/lexical_cast.hpp>
#include <boost/random.hpp>

// 0 for local, 1 for remote run

const bool remote = 0;

// 1 to print a video at the end

const bool makevid = 0;

// Mersenne Twister pseudo-random number generator. Cell radii are drawn from a Gaussian
// distribution, while noise values are drawn from a normal distribution.
std::chrono::time_point<std::chrono::high_resolution_clock> t1 = std::chrono::high_resolution_clock::now();
boost::mt19937 gen(std::chrono::duration_cast<std::chrono::nanoseconds>(t1.time_since_epoch()).count());
boost::uniform_real<> unidist(-PI, PI);
boost::normal_distribution<> normdist(0, 1);
boost::variate_generator< boost::mt19937, boost::uniform_real<> > randuni(gen, unidist);
boost::variate_generator< boost::mt19937, boost::normal_distribution<> > randnorm(gen, normdist);

struct Engine
{
    Engine(string, string, long int, long int, double, double, double);
    ~Engine();
	
    string location;
    
    int N;                              // Number of cells
    double CFself;                      // Self-propulsion force
    double CTnoise;                     // Noise parameter
    double dens;                        // Packing fraction
    string run;                         // Run with current set of parameters
    string fullRun;                     // Entire set of runs
	
    const double dt = 0.1;              // Time step, in units of cell-cell repulsion time. Fix at 10
    const int nSkip = 100;              // Print data every nSkip steps
	const int film  = nSkip*100;        // Make a video of the last time steps
	const int fluct_int = 10;			// How often to measure density fluctuations
	
    long int totalSteps;
    long int countdown;
    long int t;
    long int resetCounter;              // Records the number of times the Verlet skin list is refreshed
	
    int timeAvg;             			// Number of instances to average correlation functions
    int tCorrelation;      				// Number of time steps of auto-correlation function
    int cutoff;              			// Cutoff distance for spatial correlation functions
	
    void start();
    void topology();
    void initCells();
    void assignCellsToGrid();
    void buildVerletLists();
    void relax();
    bool newSkinList();
    void calculate_next_positions();
    void neighborInteractions();
    void calculate_COM();
    void saveOldPositions();
    double calculateOrderParameter();
    vector<double> calculateSystemOrientation();
    void print_video(Print&);
    double delta_norm(double);
    double random_projection(double);
    double MSD();
    
    vector<double> COM;                 // Current position of center of mass, with no PBC
    vector<double> COM0;                // Initial position of center of mass for measuring MSD
    vector<double> COM_old;             // Stores old center of mass value for Verlet list skin refresh
    double orderAvg, order2Avg, order4Avg;
    double binder, variance;
    
    vector<Cell> cell;
    vector<Box> grid;                   // Stores topology of simulation area
    vector<vector<int>> boxPairs;       // List of box pairs that are separated by less than a correlation cut-off
    
    double L;                           // Length of the simulation area
    double Lover2;                      // Read: "L-over-two", so we don't have to calculate L/2 every time we need it
    double lp;                          // Length of one box in the grid
    int b;                              // Number of boxes in one dimension
    int nbox;                           // Total number of boxes
    int nboxnb;                         // Number of boxes neighboring each other: 9 in 2D, 27 in 3D
	
    // Note that the performance of the algorithm depends highly on the choice of rn and rs.
	// Rs should not be so large as to include next-nearest neighbors, because then the algorithm
	// checks a much bigger Verlet list at every time step. On the other hand, a bigger rs-rn
	// means fewer global list refreshes, and the global list refreshes are more computationally
	// expensive.
	
    const double rn = 2.8;              // Radius that defines particle's interaction neighborhood
    const double rs = 1.5*rn;           // Verlet skin radius
    const double rn2 = rn*rn;
    const double rs2 = rs*rs;

};

Engine::Engine(string dir, string ID, long int n, long int steps, double l_s, double l_n, double rho)
// Constructor
{
    fullRun     = dir;
    run         = ID;
    N           = n;
    totalSteps  = steps;
    countdown   = steps+1;
    CFself      = l_s;
    CTnoise     = l_n;
    dens        = rho;
    
    t = 0;
    resetCounter = 0;
    
    orderAvg = 0.0;
    order2Avg = 0.0;
    order4Avg = 0.0;
    binder = 0.0;
    variance = 0.0;
    
    COM.assign(NDIM,0.0);
    COM0.assign(NDIM,0.0);
    COM_old.assign(NDIM,0.0);
    
    if(NDIM==2) nboxnb=9;
    if(NDIM==3) nboxnb=27;
	
	if( remote == 0 )
	{
		location = "/Users/Daniel1/Desktop/ActiveMatterResearch/jamming-dynamics/";
		timeAvg = 10;
    	tCorrelation = 10;
    	cutoff = 20;
	}
	
	else if ( remote == 1 )
	{
		location = "/home/dmccusker/remote/jamming-dynamics/";
		timeAvg = 100;
    	tCorrelation = 100;
    	if(NDIM == 2) cutoff = 140;
    	if(NDIM == 3) cutoff = 70;
	}
	
}

Engine::~Engine()
// Destructor
{
    for (int i=0; i<N; i++) cell[i].VerletList.clear();
    for (int j=0; j<nbox; j++) grid[j].CellList.clear();
}

void Engine::start()
{
    high_resolution_clock::time_point t1 = high_resolution_clock::now();
    
    initCells();
    topology();
    
    Print printer(location, fullRun, run, N, remote);
    Fluctuations fluct(L, totalSteps, fluct_int, dens);
    Correlations corr(L, dens, cutoff, tCorrelation, N, CFself);
    
    assignCellsToGrid();
    buildVerletLists();
    
    relax();
	
	// Store cells' initial positions.
	
	for(int i=0; i<N; i++){
		for(int k=0; k<NDIM; k++) {
            cell[i].x_real[k] = cell[i].x[k];
            cell[i].x0[k] = cell[i].x[k];
        }
    }
	
	// Store initial center of mass.
	
    calculate_COM();
    COM0 = COM;
	
    saveOldPositions();
    
    int corrCounter = 0;
    
    while(countdown != 0){
       
        calculate_next_positions();
		
        if(t%fluct_int == 0)
        {
			fluct.measureFluctuations(cell, COM, printer);
		}
        
        // Print data every nSkip steps.
        
        if(t%nSkip == 0)
        {
            fluct.measureFluctuations(cell, COM, printer);
            
            double order = calculateOrderParameter();
            vector<double> orientation = calculateSystemOrientation();
            
            double order2 = order*order;
            orderAvg+=order;
            order2Avg+=order2;
            order4Avg+=order2*order2;
            
            printer.print_COM(t, COM);
            printer.print_order(t, order);
            printer.print_orientation(t, orientation);
            printer.print_MSD(t, MSD());
            
            if(countdown<film && makevid)
            {
                print_video(printer);
            }
        }
        
        // Calculate static correlation functions and initialize autocorrelation function.
        
        if( t%(totalSteps/timeAvg) == 0 && t!=0 )
        {
            assignCellsToGrid();
            buildVerletLists();
            
            corr.orientation0 = calculateSystemOrientation();
            corr.spatialCorrelations(boxPairs, grid, cell);
            corr.velDist(cell);
            
            fluct.density_distribution(cell, grid);
            
            corrCounter = 0;
        }
        
        // Calculate autocorrelation function.
        
        if( corrCounter < tCorrelation )
        {
            vector<double> orient = calculateSystemOrientation();
            corr.autocorrelation( corrCounter, orient );
            corrCounter++;
        }
        
        t++;
        countdown--;
    }
    
    orderAvg  /= (double)totalSteps/(double)nSkip;
    order2Avg /= (double)totalSteps/(double)nSkip;
    order4Avg /= (double)totalSteps/(double)nSkip;
    binder = 1.0 - order4Avg/(3.0*order2Avg*order2Avg);
    variance = order2Avg - orderAvg*orderAvg;
    
    corr.printCorrelations(timeAvg, printer);
    fluct.print_density_distribution(timeAvg, printer);
    
    high_resolution_clock::time_point t2 = high_resolution_clock::now();
    auto duration = duration_cast<seconds>( t2 - t1 ).count();
    printer.print_summary(	run, N, L, t, 1./dt, CFself, CTnoise, dens, duration, resetCounter,
							binder, orderAvg, variance	);
}

void Engine::initCells()
{
    double volume = 0;
    
    // Create vector of cell objects and assign their radii.

    for(int i=0; i<N; i++){
        Cell *newCell = new Cell();
        cell.push_back(*newCell);
        cell[i].index = i;
        
        double cellRad = 1. + randnorm()/10;
        cell[i].R  = cellRad;
        cell[i].Rinv = 1.0/cellRad;
        if(NDIM==2) volume += cellRad*cellRad;
        if(NDIM==3) volume += cellRad*cellRad*cellRad;
    }
    
    // Simulation area/volume depends on cell sizes.
    
    if(NDIM==2) L = sqrt( PI * volume / dens );
    if(NDIM==3) L = cbrt( 4.0 * PI * volume / (3.0*dens) );
    Lover2 = L/2.0;
    
    // Set up initial cell configuration in an hexagonal lattice, apply periodic boundaries.
    
    int rootN = 0;
    if(NDIM==2) rootN = sqrt(N);
    if(NDIM==3) rootN = cbrt(N);
    double spacing = L/rootN;
    
    for (int i=0; i<N; i++) {
        
        cell[i].L = L;
        cell[i].Lover2 = Lover2;
        cell[i].dt = dt;
        
        int j = i/rootN;
        int k = i/(rootN*rootN);
        
        if(NDIM==2){
            cell[i].x[0]    = -Lover2 + spacing*(i%rootN) + randnorm()/10.;
            cell[i].x[1]    = -Lover2 + spacing*j + randnorm()/10.;
            if( j%2 == 0 ) { cell[i].x[0] += 1.0; }
            
            cell[i].theta = PI/2.0;
            cell[i].phi = randuni();
            cell[i].cosp = cos(cell[i].phi);
            cell[i].sinp = sin(cell[i].phi);
        }
        
        if(NDIM==3) {
            cell[i].x[0] = -Lover2 + spacing*(i%rootN) + randnorm()/10.;
            cell[i].x[1] = -Lover2 + spacing*j + randnorm()/10. - L*k ;
            cell[i].x[2] = -Lover2 + spacing*k + randnorm()/10.;
            if( j%2 == 0 ) { cell[i].x[0] += 1.0; }
            if( k%2 == 0 ) { cell[i].x[1] += 1.0; }
            
            cell[i].theta = (randuni() + PI)/2.0;
            cell[i].phi = randuni();
            cell[i].cosp = cos(cell[i].phi);
            cell[i].sinp = sin(cell[i].phi);
            cell[i].cost = cos(cell[i].theta);
            cell[i].sint = sin(cell[i].theta);
        }
        
        cell[i].periodicAngles();
        cell[i].PBC();
    }
}

void Engine::topology()
// lp is ~at least~ the assigned neighbor region diameter. It can be a little bit bigger such that
// we have an integer number of equally-sized boxes.
// Dynamics are incorrect if b<2 (small number of particles).
{
    lp = 2*rn;
    b = static_cast<int>(floor(L/lp));
    if(NDIM==2) nbox = b*b;
    if(NDIM==3) nbox = b*b*b;
    lp = L/floor(L/lp);
    
    for (int k=0; k<nbox; k++) {
        Box *newBox = new Box;
        grid.push_back(*newBox);
        grid[k].serial_index = k;
    }
    
    // Label box indices, and find each box's neighbors. Including itself, each box has 9 neighbors
    // in 2D and 27 in 3D.
    
    boxPairs.reserve(b*(b+1)/2);
    
    if(NDIM==2){
        for(int i=0; i<b; i++){
            for(int j=0; j<b; j++){
                int p = i+(j*b);
                grid[p].vector_index[0] = i;
                grid[p].vector_index[1] = j;
                
                grid[p].min[0] = -Lover2 + i*L/b;
                grid[p].min[1] = -Lover2 + j*L/b;
                grid[p].max[0] = -Lover2 + (i+1)*L/b;
                grid[p].max[1] = -Lover2 + (j+1)*L/b;
               
                for (int m=0; m<NDIM; m++) {
                    grid[p].center[m] = (grid[p].min[m] + grid[p].max[m]) / 2.;
                }
            }
        }
        for(int k=0; k<nbox; k++){
            for (int j=0; j<3; j++) {
                for (int i=0; i<3; i++){
                    int p = i;
                    int q = j;
                    
                    if (grid[k].vector_index[0] == 0   && i==0) p=p+b;
                    if (grid[k].vector_index[0] == b-1 && i==2) p=p-b;
                    if (grid[k].vector_index[1] == 0   && j==0) q=q+b;
                    if (grid[k].vector_index[1] == b-1 && j==2) q=q-b;
                    
                    grid[k].neighbors[i+(j*3)] = k + (p-1) + (q-1)*b;
                }
            }
        }
    }
    
    if(NDIM==3){
        for(int i=0; i<b; i++){
            for(int j=0; j<b; j++){
                for(int k=0; k<b; k++){
                    int p = i+(j*b)+(k*b*b);
                    grid[p].vector_index[0] = i;
                    grid[p].vector_index[1] = j;
                    grid[p].vector_index[2] = k;

                    grid[p].min[0] = -Lover2 + i*lp;
                    grid[p].min[1] = -Lover2 + j*lp;
                    grid[p].min[2] = -Lover2 + k*lp;
                    grid[p].max[0] = grid[p].min[0] + lp;
                    grid[p].max[1] = grid[p].min[1] + lp;
                    grid[p].max[2] = grid[p].min[2] + lp;
                    
                    for (int m=0; m<NDIM; m++) {
                        grid[p].center[m] = (grid[p].min[m] + grid[p].max[m]) / 2.;
                    }
                }
            }
        }
        for(int p=0; p<nbox; p++){
            for (int i=0; i<3; i++) {
                for (int j=0; j<3; j++){
                    for (int k=0; k<3; k++){
                        int a1 = i;
                        int a2 = j;
                        int a3 = k;
                        
                        if (grid[p].vector_index[0] == 0   && i==0) a1=a1+b;
                        if (grid[p].vector_index[0] == b-1 && i==2) a1=a1-b;
                        if (grid[p].vector_index[1] == 0   && j==0) a2=a2+b;
                        if (grid[p].vector_index[1] == b-1 && j==2) a2=a2-b;
                        if (grid[p].vector_index[2] == 0   && k==0) a3=a3+b;
                        if (grid[p].vector_index[2] == b-1 && k==2) a3=a3-b;
                        
                        grid[p].neighbors[i+(j*3)+(k*9)] = p + (a1-1) + (a2-1)*b + (a3-1)*b*b;
                    }
                }
            }
        }
    }
    
    // Build a list of boxes whose entire areas are separated by less than the cutoff
    // radius. Include the entire diagonal length of the boxes, not just their center-center
    // distance, so add sqrt2(3)*lp to the cutoff distance.
    
    for (int p=0; p<nbox; p++){
        for(int q=p; q<nbox; q++){
            double boxDist2 = 0.0;
            for (int k=0; k<NDIM; k++){
                double dr = delta_norm(grid[p].center[k]-grid[q].center[k]);
                boxDist2+=dr*dr;
            }
            double boxCutoff = 0.0;
            if(NDIM==2) boxCutoff = cutoff+(sqrt2*lp);
            if(NDIM==3) boxCutoff = cutoff+(sqrt3*lp);
            if (boxDist2 < boxCutoff*boxCutoff)
            {
                vector<int> temp;
                temp.assign(2,0);
                temp[0] = grid[p].serial_index;
                temp[1] = grid[q].serial_index;
                boxPairs.push_back(temp);
            }
        }
    }
}

void Engine::relax()
// Relax the system as passive particles to allow many rearrangements.
// Then, allow to thermalize, slowly increasing activity to final value.
{
 	int trelax = 0;
    int tthermalize = 0;
	
    if( remote == 0 )
	{
		trelax = 2000;
		tthermalize = 2000;
	}
	
	else if ( remote == 1 )
	{
		trelax = (int)(1000.0/dt);
		tthermalize = 1e6;
	}
	
    double CFself_old = CFself;
    
    CFself = 0;
    
    for(int t_=0; t_<trelax; t_++)
    {
        for(int i=0; i<N; i++)
        {
            cell[i].phi = randuni();
            if(NDIM==3) cell[i].theta = (randuni()+PI)/2.0;
        }
        
        calculate_next_positions();
    }
    
    for(int t_=0; t_<tthermalize; t_++)
    {
        CFself = CFself_old - (tthermalize - t_)*CFself_old/tthermalize;
        calculate_next_positions();
    }
    
    CFself = CFself_old;
	
    resetCounter = 0;
}

void Engine::assignCellsToGrid()
// A cell can be, at most, a distance of lp*√3/2 (3D) or lp*√2/2 (2D) from its closest box center.
// Starting at this distance, search through all boxes to find which box the cell is in.
{
    for (int j=0; j<nbox; j++) grid[j].CellList.clear();
    
    for (int i=0; i<N; i++)
    {
        double r2 = lp*lp*0.25*NDIM;
        for (int j=0; j<nbox; j++)
        {
            double d2 = 0.0;
            for (int k=0; k<NDIM; k++)
            {
                double dr = cell[i].x[k] - grid[j].center[k];
                d2 += dr*dr;
            }
            if(d2 < r2) { r2 = d2; cell[i].box = j; }
        }
        grid[cell[i].box].CellList.push_back(i);
    }
}

void Engine::buildVerletLists()
{
    for(int i=0; i<N; i++)
    {
        cell[i].VerletList.clear();
        for(int m=0; m<nboxnb; m++)
        {
            int p = grid[cell[i].box].neighbors[m];
            int max = grid[p].CellList.size();
            for(int k=0; k<max; k++)
            {
                int j = grid[p].CellList[k];
                if(j > i)
                {
                    double d2 = 0.0;
                    double dx = delta_norm(cell[j].x[0] - cell[i].x[0]);
                    double dy = delta_norm(cell[j].x[1] - cell[i].x[1]);
					
                    if(NDIM==3)
                    {
                    	double dz = delta_norm(cell[j].x[2] - cell[i].x[2]);
                    	d2 += dz*dz;
					}
					
                    d2 += dx*dx+dy*dy;
					
                    if( d2 < rs2 )
                    {
                        cell[i].VerletList.push_back(j);
                        cell[j].VerletList.push_back(i);
                    }
                }
            }
        }
    }
}

bool Engine::newSkinList()
// Compare the two largest particle displacements to see if a skin refresh is required.
// Refresh=true if any particle may have entered any other particle's neighborhood.
{
    bool refresh = false;
    double largest2 = 0.;
    double second2 = 0.;
    for(int i=0; i<N; i++){
        double d2 = 0.0;
        double dx = delta_norm(cell[i].x[0] - cell[i].x_old[0] - COM[0] + COM_old[0]);
        double dy = delta_norm(cell[i].x[1] - cell[i].x_old[1] - COM[1] + COM_old[1]);
		
		if( NDIM==3 )
		{
			double dz = delta_norm(cell[i].x[2] - cell[i].x_old[2] - COM[2] + COM_old[2]);
			d2 += dz*dz;
		}
		
		d2 += dx*dx+dy*dy;
		
        if(d2 > largest2)     { second2 = largest2; largest2 = d2; }
        else if(d2 > second2) { second2 = d2; }
    }
    
    if( ( sqrt(largest2)+sqrt(second2) ) > (rs-rn) ){
        resetCounter++;
        saveOldPositions();
        refresh = true;
    }
    return refresh;
}

void Engine::neighborInteractions()
// *** Most physics happens here *** //
// Calculate spring repulsion force and neighbor orientational interactions.
{
    if(NDIM==2)
    {
        for(int i=0; i<N; i++)
        {
            int max = cell[i].VerletList.size();
            for(int k=0; k<max; k++)                                // Check each cell's Verlet list for neighbors
            {
                int j = cell[i].VerletList[k];
                if(j > i)                                           // Symmetry reduces calculations by half
                {
                    double dx = delta_norm(cell[j].x[0]-cell[i].x[0]);
                    double dy = delta_norm(cell[j].x[1]-cell[i].x[1]);
                    double d2 = dx*dx+dy*dy;
                    
                    if(d2 < rn2)                                    // They're neighbors
                    {
                        double sumR = cell[i].R + cell[j].R;
                    
                        if( d2 < sumR*sumR )                        // They also overlap
                        {
                            double overlap = sumR / sqrt(d2) - 1;
                                                                    // Spring repulsion force, watch the sign
							double fx = overlap*dx;
                        	double fy = overlap*dy;
							
                            cell[i].Fx -= fx;
                            cell[j].Fx += fx;
                            cell[i].Fy -= fy;
                            cell[j].Fy += fy;
                            
                            if(countdown <= film && t%nSkip == 0){
                                cell[i].over -= 240*abs(overlap);
                                cell[j].over -= 240*abs(overlap);
                            }
                        }
                    
                        cell[i].x_new += cell[j].cosp;                  // Add up orientations of neighbors
                        cell[i].y_new += cell[j].sinp;
                        cell[j].x_new += cell[i].cosp;
                        cell[j].y_new += cell[i].sinp;
                    }
                }
            }
			
            cell[i].phi   = atan2(cell[i].y_new, cell[i].x_new) + CTnoise*randuni();
        }
    }
    
    else if(NDIM==3)
    {
        for(int i=0; i<N; i++)
        {
            int max = cell[i].VerletList.size();
            for(int k=0; k<max; k++)
            {
                int j = cell[i].VerletList[k];
                if(j > i)
                {
                    double dx = delta_norm(cell[j].x[0]-cell[i].x[0]);
                    double dy = delta_norm(cell[j].x[1]-cell[i].x[1]);
                    double dz = delta_norm(cell[j].x[2]-cell[i].x[2]);
                    double d2 = dx*dx+dy*dy+dz*dz;
                    
                    if(d2 < rn2)
                    {
                        double sumR = cell[i].R + cell[j].R;
                        
                        if( d2 < sumR*sumR )
                        {
                            double overlap = sumR / sqrt(d2) - 1;
							
                            double fx = overlap*dx;
                        	double fy = overlap*dy;
                        	double fz = overlap*dz;
                            
                            cell[i].Fx -= fx;
                            cell[j].Fx += fx;
                            cell[i].Fy -= fy;
                            cell[j].Fy += fy;
                            cell[i].Fz -= fz;
                            cell[j].Fz += fz;
                            
                            if(countdown <= film && t%nSkip == 0)
                            {
                                cell[i].over -= 240*abs(overlap);
                                cell[j].over -= 240*abs(overlap);
                            }
                        }
                        
                        cell[i].x_new += cell[j].sint*cell[j].cosp;
                        cell[i].y_new += cell[j].sint*cell[j].sinp;
                        cell[i].z_new += cell[j].cost;

                        cell[j].x_new += cell[i].sint*cell[i].cosp;
                        cell[j].y_new += cell[i].sint*cell[i].sinp;
                        cell[j].z_new += cell[i].cost;
                    }
                }
            }
            
            double norm = sqrt(  cell[i].x_new*cell[i].x_new
                               + cell[i].y_new*cell[i].y_new
                               + cell[i].z_new*cell[i].z_new ) ;
            
            // New cell orientation, without noise
            cell[i].x_new /= norm;
            cell[i].y_new /= norm;
            cell[i].z_new /= norm;
            
            // Random vector v within a spherical cap around z-axis, defined by CTnoise
            double phi = randuni();
            double vz = random_projection(cos(CTnoise*PI));
            double vy = cos(phi)*sqrt(1.0-vz*vz);
            double vx = sin(phi)*sqrt(1.0-vz*vz);
            
            // Rotation axis from cross product: (0,0,1) x new cell orientation
            double kx = -cell[i].y_new;
            double ky = cell[i].x_new;
            double crossnorm = sqrt(kx*kx+ky*ky);
            kx /= crossnorm;
            ky /= crossnorm;
            
            // Rotation angle given by dot product: cos(angle) = new_vector . (0,0,1)
            double cosa = cell[i].z_new;
            double sina = sin(acos(cell[i].z_new));
            
            // Apply Rodrigues rotation formula
            double dot = (1.0-cosa)*(kx*vx+ky*vy);
            cell[i].x_new = cosa*vx + sina*ky*vz + dot*kx;
            cell[i].y_new = cosa*vy - sina*kx*vz + dot*ky;
            cell[i].z_new = cosa*vz + sina*(kx*vy-ky*vx);
            
            cell[i].phi = atan2(cell[i].y_new, cell[i].x_new);
            cell[i].theta = acos(cell[i].z_new);
        }
    }
}

void Engine::calculate_COM()
{
    for(int k=0; k<NDIM; k++)
    {
        COM[k] = 0.0;
        
        for(int i=0; i<N; i++)
        {
            COM[k] += cell[i].x_real[k];
        }
		
        COM[k] /= N;
    }
}

double Engine::calculateOrderParameter()
{
    vector<double> orient(3,0.0);
    
    for (int i=0; i<N; i++)
    {
    	double inverseVel = 1.0/cell[i].get_speed();
        orient[0] += cell[i].vx*inverseVel;
        orient[1] += cell[i].vy*inverseVel;
        if(NDIM==3) orient[2] += cell[i].vz*inverseVel;
    }
   
    return sqrt(orient[0]*orient[0]+orient[1]*orient[1]+orient[2]*orient[2])/(double)N;
}

vector<double> Engine::calculateSystemOrientation()
{
   	vector<double> orient(3,0.0);
	
    for (int i=0; i<N; i++)
    {
    	double inverseVel = 1.0/cell[i].get_speed();
        orient[0] += cell[i].vx*inverseVel;
        orient[1] += cell[i].vy*inverseVel;
        if(NDIM==3) orient[2] += cell[i].vz*inverseVel;
    }
    
    for (int k=0; k<NDIM; k++ ) orient[k] /= (double)N;
    
    return orient;
}

double Engine::MSD()
{
    double MSD = 0.0;
    for (int i=0; i<N; i++)
    {
    	double dx = cell[i].x_real[0] - cell[i].x0[0] - COM[0] + COM0[0];
    	double dy = cell[i].x_real[1] - cell[i].x0[1] - COM[1] + COM0[1];
    	double dz = 0.0;
		if( NDIM==3 )
		{
			dz = cell[i].x_real[2] - cell[i].x0[2] - COM[2] + COM0[2];
		}
		MSD += dx*dx+dy*dy+dz*dz;
    }
    return MSD/N;
}

void Engine::saveOldPositions()
{
    for(int k=0; k<NDIM; k++)
    {
        COM_old[k] = COM[k];
        for(int i=0; i<N; i++)
        {
            cell[i].x_old[k]  = cell[i].x[k];
        }
    }
}

void Engine::calculate_next_positions()
{
    if( newSkinList() )
    {
        assignCellsToGrid();
        buildVerletLists();
    }
    
    neighborInteractions();
    
    for(int i=0; i<N; i++)
    {
        cell[i].update(CFself);
    }
    
    calculate_COM();
}

void Engine::print_video(Print &printer)
{
    int k=0;
    for(int i=0; i<N; i++)
    {
    	vector<double> velocity(3,0.0);
		
    	velocity[0] = cell[i].vx;
    	velocity[1] = cell[i].vy;
    	velocity[2] = cell[i].vz;
		
        printer.print_Ovito(k, N, i, cell[i].R, cell[i].over, cell[i].x, velocity);
        cell[i].over = 240;
        k=1;
    }
}

double Engine::delta_norm(double delta)
// Subtracts multiples of the box size to account for periodic boundary conditions
{
    int k=-1;
    if(delta < -Lover2) k=1;
    while(delta < -Lover2 || delta >= Lover2) delta += k*L;
    
    return delta;
}

double Engine::random_projection(double cost)
{
    boost::uniform_real<> dist(cost, 1);
    boost::variate_generator<boost::mt19937&, boost::uniform_real<> > z_project(gen, dist);
    return z_project();
}

int main(int argc, char *argv[])
{
    string dir = "";
    string ID = "";
    long int n = 0;
    long int steps = 0;
    double l_s = 0;
    double l_n = 0;
    double rho = 0;
    
    int npoints = 0;
    
    if(argc != 8){
        cout    << "Incorrect number of arguments. Need: " << endl
        << "- full run ID" << endl
        << "- single run ID" << endl
        << "- number of cells" << endl
        << "- number of steps" << endl
        << "- \\lambda_s" << endl
        << "- \\lambda_n" << endl
        << "- \\rho" << endl
        << "Program exit status (1)" << endl;
        return 1;
    }
    else
    {
        dir     = argv[1];
        ID      = argv[2];
        n       = atol(argv[3]);
        steps   = atol(argv[4]);
        l_s     = atof(argv[5]);
        l_n     = atof(argv[6]);
        rho     = atof(argv[7]);
        
        Engine engine(dir, ID, n, steps, l_s, l_n, rho);
        engine.start();
        npoints = steps/engine.nSkip;
    }
	
    return 0;
}
