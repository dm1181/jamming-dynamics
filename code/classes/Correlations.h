
// ********************************************************************************
// **** Methods for calculating correlation functions and velocity distribution ***
// ********************************************************************************

// *** Gets NDIM from definition in main ***

struct Correlations
{
    Correlations(double, double, double, double, long int, double);
    
    void spatialCorrelations(vector<vector<int>>&, vector<Box>&, vector<Cell>&);
    void autocorrelation(int, vector<double>& );
    void velDist(vector<Cell>&);
    
    void printCorrelations(int, Print&);
    double delta_norm(double);
    
    double L, Lover2, dens;
    long int N;
    
    int correlation_time;  // Time step cut-off for autocorrelation
    double cutoff;
    
    double dr_c;
    double dr_p;
    double dv;
    int np;
    int nc;
    int noBins;
    double norm;
    
    vector<double> orientation0;
    vector<double> orientationCorrelation;
    vector<double> velocityCorrelation;
    vector<double> pairCorrelationValues;
    vector<double> autocorrelationValues;
    vector<double> velocityDistributionValues;
    
};

Correlations::Correlations(double L_, double dens_, double cut, double time, long int N_, double CFself_)
{
    N = N_;
    L = L_;
    Lover2 = L/2.0;
    dens = dens_;
    
    cutoff = cut;
    correlation_time = time;
    
    dr_c = 2.0;
    dr_p = 0.1;
    dv = CFself_/50.0;
    np = (int)ceil(cutoff/dr_p);
    nc = (int)ceil(cutoff/dr_c);
    noBins = 100;

    if(NDIM == 2) norm = 2*L*L/(2*PI*dr_p*(double)(N*N));
    if(NDIM == 3) norm = 2*L*L*L/(4*PI*dr_p*(double)(N*N));
    
    orientation0.assign(NDIM,0);
    
    orientationCorrelation.assign(nc, 0);
    velocityCorrelation.assign(nc,0);
    pairCorrelationValues.assign(np, 0);
    autocorrelationValues.assign(correlation_time,0);
    velocityDistributionValues.assign(noBins,0);
}

void Correlations::spatialCorrelations
    (vector<vector<int>> &boxPairs, vector<Box> &grid, vector<Cell> &cell)
// We normalize the velocity correlations by the number of counts in the bin size. The pair
// correlation normalization is geometric and depends on the system dimension.
{
    vector<double> pairTemp(np,0.0);
    vector<double> corrTemp(nc,0.0);
    vector<double> velTemp(nc,0.0);
    vector<double> counts(nc,0.0);

    for (int a=0; a<boxPairs.size(); a++)
    {
        int p = boxPairs[a][0];
        int q = boxPairs[a][1];
        int maxp = grid[p].CellList.size();
        int maxq = grid[q].CellList.size();
        
        for (int m=0; m<maxp; m++)
        {
            for (int n=0; n<maxq; n++)
            {
                int i = grid[p].CellList[m];
                int j = grid[q].CellList[n];
                
                // Avoid double-counting pairs in the same box.
                
                if( p!=q || (p==q && j>i) )
                {
                    double r = 0.0;
                    for(int k=0; k<NDIM; k++){
                        double dk = delta_norm(cell[j].x[k]-cell[i].x[k]);
                        r += dk*dk;
                    }
                    
                    r = sqrt(r);
                    
                    int binp = (int)floor(r/dr_p);
                    int binc = (int)floor(r/dr_c);
                    
                    // Exclude any pairs beyond cutoff, normalize.
                    
                    if(binp < np)
                    {
                        if(NDIM == 2) pairTemp[binp] += 1.0/r;
                        if(NDIM == 3) pairTemp[binp] += 1.0/(r*r);
                    }
                    
                    if(binc < nc)
                    {
                    	counts[binc] += 1.0;
						
                        double vxi = cell[i].vx;
                        double vyi = cell[i].vy;
                        double vxj = cell[j].vx;
                        double vyj = cell[j].vy;
                        
                        if(NDIM == 2)
                        {
                            corrTemp[binc] += cell[i].cosp*cell[j].cosp
											+ cell[i].sinp*cell[j].sinp;
                            velTemp[binc] +=
                            		(vxi*vxj+vyi*vyj)/(cell[i].get_speed()*cell[j].get_speed());
                        }
                        if(NDIM == 3)
                        {
                        	double ti = cell[i].theta;
                        	double tj = cell[j].theta;
                        	double pi = cell[i].phi;
                        	double pj = cell[i].phi;
							
                            corrTemp[binc] += ( cos(ti)*cos(tj) + cos(pi-pj)*sin(ti)*sin(tj) ) ;
							
							double vzi = cell[i].vz;
                            double vzj = cell[j].vz;
                            velTemp[binc] +=
                                (vxi*vxj+vyi*vyj+vzi*vzj)/(cell[i].get_speed()*cell[j].get_speed());
                        }
                    }
                }
            }
        }
    }
    
    for(int k=0; k<nc; k++)
    {
        corrTemp[k] /= counts[k];
        velTemp[k] /= counts[k];
        orientationCorrelation[k] += corrTemp[k];
        velocityCorrelation[k] += velTemp[k];
    }
    
    for(int k=0; k<np; k++)
    {
    	pairTemp[k] *= norm;
		pairCorrelationValues[k]+=pairTemp[k];
	}
}

void Correlations::autocorrelation(int t, vector<double>&orientation)
{
    double value = 0.0;
    for(int k=0; k<NDIM; k++)
    {
    	value += orientation0[k]*orientation[k];
	}
    autocorrelationValues[t] += value;
}

void Correlations::velDist(vector<Cell> &cell)
{
    for(int i=0; i<N; i++)
    {
        double v = cell[i].get_speed();
        int binv = (int)floor(v/dv);
        if(binv < noBins) velocityDistributionValues[binv] += 1.0/(double)N;
    }
}

void Correlations::printCorrelations(int timeAvg, Print &print)
{
    for(int k=0; k<velocityCorrelation.size(); k++)
        print.print_corr(dr_c*(k+1), velocityCorrelation[k]/timeAvg);
	
	for(int k=0; k<orientationCorrelation.size(); k++)
        print.print_orientationCorr(dr_c*(k+1), orientationCorrelation[k]/timeAvg);
    
    for(int k=0; k<pairCorrelationValues.size(); k++)
        print.print_pairCorr(0.01*k, pairCorrelationValues[k]/timeAvg);
    
    for(int t=0; t<autocorrelationValues.size(); t++)
        print.print_autoCorr(t, autocorrelationValues[t]/timeAvg);
    
    for(int k=0; k<velocityDistributionValues.size(); k++)
        print.print_velDist(k*dv, velocityDistributionValues[k]/timeAvg);
}

double Correlations::delta_norm(double delta)
// Subtracts multiples of the box size to account for periodic boundary conditions
{
    int k=-1;
    if(delta < -Lover2) k=1;
    while(delta < -Lover2 || delta >= Lover2) delta += k*L;
    
    return delta;
}
