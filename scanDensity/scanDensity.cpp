/*Scanner units are proabaly in meters */
#include <Eigen/Dense>
#include <Eigen/StdVector>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <stdio.h>
#include <iostream>
#include <string>
#include <dirent.h>
#include <vector>
#include <fstream>
#include <math.h>
#include <time.h>

/*#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/io/ply_io.h>*/

using namespace cv;
using namespace std;
using namespace Eigen;
// using namespace pcl;

float scale = 75.0;

void examinePointEvidence(const vector<Vector3f > &, const float *, const float *,
	const string &, const string &);
void createBoundingBox(float *, float *, float*, const vector<Vector3f > &);
void examineFreeSpaceEvidence(const vector<Vector3f > &, const float*,
	const float*, const float*, 
	const string &, const string &);
void showSlices(const vector<MatrixXi>  &, const int, const int, const int);
void collapseFreeSpaceEvidence(const vector<MatrixXi> &, const int, const int,
	const int, const string &, const string &);
void displayCollapsed(const vector<VectorXi> &, const int, const int, const string &);
void displayPointEvenidence(const vector<VectorXf> &, const string &, const int);

void analyzeScan(const string &, const string &);


Mat heatMap;
bool pointE = true;
bool freeE = true;

void usage(){
	cout << "Usage: scanDensity.o <Dataflag> <binaryData> <output" 
			<< "Folder>/ Optional:scale Optional:<TypeFlag>" << endl;
	cout << "Data flags: -f:  Tells the program to expect a folder of binary files" << endl;
	cout << "            -s:  Tells the program to expect a single binary files" << endl;
	cout << "Type flags: -pe: Point evidence only" << endl;
	cout << "            -fe: Free space evidence only" << endl;
	exit(-1);
}

int main(int argc, char const *argv[])
{

	if(argc != 4 && argc != 5 && argc !=6)
		usage();

	if(argc >= 5){
		string scaleString = argv[4];
		scale = stof(scaleString);
	}

	if(argc == 6){
		string typeFlag = argv[5];
		if(typeFlag.compare("-pe")==0)
			freeE = false;
		if(typeFlag.compare("-fe")==0)
			pointE = false;
	}
	
	size_t startTime, endTime;
	startTime = clock();
	cvNamedWindow("Preview", WINDOW_NORMAL);

	vector<string> binaryNames;

	string dataFlag = argv[1];
	const char * fileFolderIn = argv[2];
	const string fileFolderOut = argv[3];

	if(dataFlag.compare("-f") == 0){
		DIR *dir;
		struct dirent *ent;
		if ((dir = opendir (fileFolderIn)) != NULL) {
		  /* Add all the files and directories to a vector */
		  while ((ent = readdir (dir)) != NULL) {
		  	string fileName = ent->d_name;
		  	if(fileName != ".." && fileName != "."){
		  		binaryNames.push_back(fileName);
		  	}
		  }
		  closedir (dir);
		}  else {
		  /* could not open directory */
		  perror ("");
		  return EXIT_FAILURE;
		}

		sort(binaryNames.begin(), binaryNames.end());
		for(auto & name : binaryNames){
			const string binaryFilePath = fileFolderIn + name;
			analyzeScan(binaryFilePath, fileFolderOut);
		}
	} else{
		analyzeScan(string (fileFolderIn), fileFolderOut);
	}
	
	endTime = clock();
	float seconds = static_cast<float>(endTime - startTime)/CLOCKS_PER_SEC;
	cout << "Time: " << seconds << "s" << endl;
	
	



	
	

	return 0;
}

void analyzeScan(const string & fileName, const string & outputFolder){
	const string scanNumber = fileName.substr(fileName.find(".") - 3, 3);
	cout << scanNumber << endl;
    ifstream scanFile (fileName, ios::in | ios::binary);

    int columns, rows;
   	scanFile.read(reinterpret_cast<char *> (& columns), sizeof(int));
   	scanFile.read(reinterpret_cast<char *> (& rows), sizeof(int));
    

    
    float pointMax [3], pointMin[3];
    pointMax[0] = pointMax[1] = pointMax[2] 
    	= pointMin[0] = pointMin[1] = pointMin[2] = 0;
    
    vector<Vector3f > points;
    for (int k = 0; k < columns * rows; ++k) {
	    Vector3f point;
		scanFile.read(reinterpret_cast<char *> (&point[0]), sizeof(Vector3f));
		
		if(point[0] == 0 || point[1] == 0 || point[2] == 0)
			continue;
		if(point[0]*point[0] + point[1]*point[1] < 1){
			continue;
		}
	  	
	    points.push_back(point);
	}
	scanFile.close();

	float  zNormFactors [2];
	createBoundingBox(pointMin, pointMax, zNormFactors, points);

	if(pointE)
		examinePointEvidence(points, pointMin, pointMax, outputFolder, scanNumber);

	if(freeE)
		examineFreeSpaceEvidence(points, pointMin, pointMax, zNormFactors, outputFolder, scanNumber);
}



void createBoundingBox(float * pointMin, float * pointMax, float * zNormFactors,
	const vector<Vector3f > & points){
	double averageX, averageY, sigmaX, sigmaY, averageZ, sigmaZ;
	averageX = averageY = sigmaX = sigmaY = averageZ = sigmaZ = 0;

	for (auto & point : points)
	{
		averageX += point[0];
		averageY += point[1];
		averageZ += point[2];
	}
	averageX = averageX/points.size();
	averageY = averageY/points.size();
	averageZ = averageZ/points.size();

	for (auto & point : points)
	{
		sigmaX += (point[0] - averageX)*(point[0] - averageX);
		sigmaY += (point[1] - averageY)*(point[1] - averageY);
		sigmaZ += (point[2] - averageZ)*(point[2] - averageZ);
	}
	sigmaX = sigmaX/(points.size()-1);
	sigmaY = sigmaY/(points.size()-1);
	sigmaZ = sigmaZ/(points.size()-1);
	sigmaX = sqrt(sigmaX);
	sigmaY = sqrt(sigmaY);
	sigmaZ = sqrt(sigmaZ);

	double dX = 1.1*5*sigmaX;
	double dY = 1.1*5*sigmaX;
	double dZ = 1.1*5*sigmaZ;

    pointMin[0] = averageX - dX/2;
  	pointMin[1] = averageY - dY/2;
	pointMin[2] = averageZ - dZ/2;

	pointMax[0] = averageX + dX/2;
	pointMax[1] = averageY + dY/2;
	pointMax[2] = averageZ + dZ/2;

	zNormFactors[0] = averageZ;
	zNormFactors[1] = sigmaZ;
} 

void examinePointEvidence(const vector<Vector3f > & points,
	const float* pointMin, const float * pointMax, 
	const string & outputFolder, const string & scanNumber){
	const int numZ = 20.0*ceil(pointMax[2] - pointMin[2]);
	const float zScale = (float)numZ/(pointMax[2] - pointMin[2]);

	const int numCols = scale * (pointMax[0] - pointMin[0]);
	const int numRows = scale * (pointMax[1] - pointMin[0]);

	heatMap = Mat (numRows, numCols, CV_8UC1, Scalar::all(255));

	vector<VectorXf> numTimesSeen (heatMap.rows, VectorXf::Zero(heatMap.cols));
	vector<MatrixXi> numTimesSeen3D (heatMap.rows, MatrixXi::Zero(heatMap.cols, numZ));


	// PointCloud<PointXYZ> cloud;
	for(auto & point : points){
	 	const int x = scale*(point[0] - pointMin[0]);
		const int y = scale*(point[1] - pointMin[1]);
		const int z = zScale*(point[2] - pointMin[2]);
		   
		if(x <0 || x >= heatMap.cols)
			continue;
		if(y < 0 || y >= heatMap.rows)
			continue; 
		if( z < 0 || z >= numZ)
			continue;
		    
	    numTimesSeen3D[y](x, z) +=1; 
	    /*if(y>=heatMap.rows/2 && y<= heatMap.rows/2+20
	    	&& x>=heatMap.cols/2+120 && x<=heatMap.cols/2 + 140)
	    	cloud.push_back(PointXYZ(x,y,z));*/
		
		
	}
	// io::savePLYFileBinary("output.ply",cloud);

	MatrixXf entropy = MatrixXf::Zero (heatMap.rows, heatMap.cols);
	MatrixXf total = MatrixXf::Zero (heatMap.rows, heatMap.cols);
	for(int i = 0; i < heatMap.rows; ++i)
	{
		for (int j = 0; j < heatMap.cols; ++j)
		{
			for (int k = 0; k < numZ; ++k)
			{
				if(numTimesSeen3D[i](j,k) != 0)
					total(i,j)++;
			}
			
		}
	}

	/*for(int i = 0; i < heatMap.cols; ++i)
	{
		for (int j = 0; j < heatMap.rows; ++j)
		{
			if(total(i,j) == 0)
				continue;
			for (int k = 0; k < numZ; ++k)
			{
				if(numTimesSeen3D[i](j,k) != 0)
				{
					const double tmp = 1.0/total(i,j);
					entropy(i,j) -= tmp*log(tmp);
				}
				
			}
		}
	}

	float aveEntropy, sigEntropy, aveTotal, sigTotal;
	aveTotal = aveEntropy = sigTotal = sigEntropy = 0;
	int eCount = 0;
	int tCount = 0;
	for (int i = 0; i < entropy.size(); ++i)
	{
		if(*(total.data() + i) != 0)
		{
			aveTotal += *(total.data() + i);
			tCount ++ ;
		}
		if(*(entropy.data() + i) !=0)
		{
			aveEntropy += *(entropy.data() + i);
			eCount++;
		}	
	}

	aveTotal /= tCount;
	aveEntropy /= eCount;

	for (int i = 0; i < entropy.size(); ++i)
	{
		if(*(entropy.data() + i) !=0)
			sigEntropy += (*(entropy.data() + i) - aveEntropy)*
				(*(entropy.data() + i) - aveEntropy);
		if(*(total.data() + i) !=0)
			sigTotal += (*(total.data() + i) - aveTotal)*
				(*(total.data() + i) - aveTotal);
	}

	
	sigEntropy /= eCount;
	sigTotal /= tCount;
	sigEntropy = sqrt(sigEntropy);
	sigTotal = sqrt(sigTotal);
	
	for (int i = 0; i < entropy.size(); ++i)
	{
		*(total.data() + i) = max(0.0, min(1.0,
			((*(total.data() + i) - aveTotal)/sigTotal + 1.0)/ 2.0));

		*(entropy.data() + i) = max(0.0, min(1.0,
			((*(entropy.data() + i) - aveEntropy)/sigEntropy + 1.0)/ 2.0));

		
	}


	for(int i = 0; i < heatMap.cols; ++i)
	{
		for (int j = 0; j < heatMap.rows; ++j)
		{
			numTimesSeen[i][j] = sqrt(entropy(i,j)*total(i,j));
		}
	}
	string imageName = outputFolder + "DUC_entropy_" + scanNumber + ".png";
	displayPointEvenidence(numTimesSeen, imageName, 2);*/

	for(auto & v : numTimesSeen){
		for (int i = 0; i < v.size(); ++i)
		{
			*(v.data() + i) = 0;
		}
	}


	for (int y = 0; y < total.rows(); ++y)
	{
		for (int x = 0; x < total.cols(); ++x)
		{
			numTimesSeen[y][x] = total(y,x);
		}
	}
	/*for(auto & point : points){
	 	const int x = scale*(point[0] - pointMin[0]);
		const int y = scale*(point[1] - pointMin[1]);
		const int z = zScale*(point[2] - pointMin[2]);
		   
		if(x <0 || x >= heatMap.cols)
			continue;
		if(y < 0 || y >= heatMap.rows)
			continue; 
		if( z < 0 || z >= numZ)
			continue;
		
	    numTimesSeen[x][y] +=1; 
	}*/

	const string imageName = outputFolder + "DUC_point_" + scanNumber + ".png";
	displayPointEvenidence(numTimesSeen, imageName, 2);
}

void displayPointEvenidence(const vector<VectorXf> & numTimesSeen, 
	const string & imageName,
	const int bias){
	double average, sigma;
	average = sigma = 0;
	int count = 0;
	for(auto & v: numTimesSeen){
		for(int i = 0; i < v.size(); ++i){
			if(*(v.data() + i) != 0){
				count++;
				average+= *(v.data() + i);
				
			}
		}
	}


	average = average/count;

	for(auto & v: numTimesSeen){
		for(int i = 0; i < v.size(); ++i){
			if(*(v.data() + i) !=0)
				sigma += (*(v.data() + i) - average)*(*(v.data() + i)- average);
		}
	}

	sigma = sigma/(count-1);
	sigma = sqrt(sigma);

	for (int i = 0; i < heatMap.rows; ++i)
	{
		uchar * dst = heatMap.ptr<uchar>(i);
		
		for (int j = 0; j < heatMap.cols; ++j)
		{
			if(numTimesSeen[i][j] != 0){
				const int gray = max(0, min(255,
					 static_cast<int>(255.0 * (numTimesSeen[i][j] - average - 1.5*sigma) 
					 	/ (bias * sigma))));
				dst[j] = 255 - gray;
				/*int red, green, blue;
				if (gray < 128) {
					red = 0;
					blue = 2 * gray;
					green = 255 - blue;
				} else {
					blue = 0;
					red = 2 * (gray - 128);
					green = 255 - red;
				}
				dst[j*3] = blue;
				dst[j*3 +1] = green;
				dst[j*3 + 2] = red;*/
			}
		} 
	} 

	/*for (int y = heatMap.rows/2; y <= heatMap.rows/2+20; ++y)
	{
		for (int x = heatMap.cols/2+120; x <= heatMap.cols/2+140; ++x)
		{
			heatMap.at<uchar>(y,x) = 0;
		}
	}*/
	
	
	
	/*imshow("Preview", heatMap);
	waitKey(0);*/
	
	imwrite(imageName, heatMap);
}

void examineFreeSpaceEvidence(const vector<Vector3f> & points, 
	const float* pointMin, const float * pointMax, const float * zNormFactors,
	const string & outputFolder, const string & scanNumber){

	const float numZSimga = 2;
	const int numX = scale * (pointMax[0] - pointMin[0]);
	const int numY = scale * (pointMax[1] - pointMin[1]);
	const int numZ = 100;

	const float zScale = (float)numZ/(pointMax[2] - pointMin[2]);
	float cameraCenter [3];
	cameraCenter[0] = -1*pointMin[0];
	cameraCenter[1] = -1*pointMin[1];
	/*cameraCenter[2] = (-1*pointMin[2] - zNormFactors[0])
		/(zNormFactors[1]) + numZSimga;*/
	cameraCenter[2] = -1*pointMin[2];

	vector<MatrixXi> pointsPerVoxel (numZ, MatrixXi::Zero(numY, numX));
	vector<MatrixXi> numTimesSeen4C (numX, MatrixXi::Zero(numY, numZ));
	vector<MatrixXi> numTimesSeen (numZ, MatrixXi::Zero(numX, numY));

	

	for(auto & point : points){
		int x = floor((point[0]- pointMin[0]) * scale);
		int y = floor((point[1] - pointMin[1]) * scale);
		/*int z = floor(((point[2] - zNormFactors[0])/(zNormFactors[1]) 
			+ numZSimga) * zScale);*/
		int z = floor((point[2] - pointMin[2])*zScale);

		if(x < 0 || x >= numX)
			continue;
		if(y < 0 || y >= numY)
			continue;
		if(z < 0 || z >= numZ)
			continue;

		pointsPerVoxel[z](y,x) += 1;
	}


	for (int k = 0; k < numZ; ++k)
	{
		for (int j = 0; j < numY; ++j)
		{
			for (int i = 0; i < numX; ++i)
			{
				if(pointsPerVoxel[k](j,i)==0)
					continue;

				float ray[3];
				ray[0] = i - cameraCenter[0]*scale;
				ray[1] = j - cameraCenter[1]*scale;
				ray[2] = k - cameraCenter[2]*zScale;
				float length = sqrt(ray[0]*ray[0] + ray[1]*ray[1] + ray[2]*ray[2]);
				float unitRay[3];
				unitRay[0] = ray[0]/length;
				unitRay[1] = ray[1]/length;
				unitRay[2] = ray[2]/length;
				int voxelHit [3];
				for (int a = 1.2*(zScale+2*scale)/3; a < (int)floor(length); ++a)
				{
			
					voxelHit[0] = floor(cameraCenter[0]*scale + a*unitRay[0]);
					voxelHit[1] = floor(cameraCenter[1]*scale + a*unitRay[1]);
					voxelHit[2] = floor(cameraCenter[2]*zScale + a*unitRay[2]);

					if(voxelHit[0] < 0 || voxelHit[0] >= numX)
						continue;
					if(voxelHit[1] < 0 || voxelHit[1] >= numY)
						continue;
					if(voxelHit[2] < 0 || voxelHit[2] >= numZ)
						continue;

					numTimesSeen[voxelHit[2]](voxelHit[0], voxelHit[1])
						+= pointsPerVoxel[k](j,i);
					numTimesSeen4C[voxelHit[0]](voxelHit[1], voxelHit[2])
						+= pointsPerVoxel[k](j,i);

				}
				
			}
			
		}
	}

	collapseFreeSpaceEvidence(numTimesSeen4C, numZ, numY, numX,
	 outputFolder, scanNumber);
}

void showSlices(const vector<MatrixXi>  & numTimesSeen, const int numSlices,
	const int numZ, const int numXY){
	const int divisions = floor((float)numZ/(numSlices+4));

	for (int a = 3; a < numSlices+3; ++a)
	{
		MatrixXi currentSlice = numTimesSeen[divisions*a];
		float average, sigma;
		average, sigma = 0;
		size_t count = 0;
		
		for(int i = 0; i < currentSlice.size(); ++i)
		{
			if(*(currentSlice.data() + i) ==0)
				continue;
			average += *(currentSlice.data() + i);
			count ++;
		}

		average = average/count;

		for (int i = 0; i < currentSlice.size(); ++i)
		{
			if(*(currentSlice.data() + i) !=0)
				sigma+=(*(currentSlice.data() + i) - average)*
					(*(currentSlice.data() + i) - average);
		}
		sigma = sigma/(count - 1);
		sigma = sqrt(sigma);

		Mat sliceMap (numXY, numXY, CV_8UC3, Scalar::all(0));

		for (int j = 0; j < sliceMap.rows; ++j)
		{
			uchar * dst = sliceMap.ptr<uchar>(j);
			
			for (int i = 0; i < sliceMap.cols; ++i)
			{
				if(currentSlice(j,i) != 0){
					const int gray = max(0, min(255,
						 static_cast<int>(255.0 * (currentSlice(j,i)
						  - average) / ((3 * sigma) + 1.0) / 2.0)));
					int red, green, blue;
					if (gray < 128) {
						red = 0;
						blue = 2 * gray;
						green = 255 - blue;
					} else {
						blue = 0;
						red = 2 * (gray - 128);
						green = 255 - red;
					}
					dst[i*3] = blue;
					dst[i*3 +1] = green;
					dst[i*3 + 2] = red;
				}
			} 
		}

		imshow("Preview", sliceMap);
		waitKey(500); 

	}
}


void collapseFreeSpaceEvidence(const vector<MatrixXi> & numTimesSeen,
	const int numZ, const int numY, const int numX,
	const string & outputFolder, const string & scanNumber){

	vector<VectorXi> collapsedMean (numY, VectorXi::Zero(numX));

	for (int i = 0; i < numX; ++i)
	{
		for (int j = 0; j < numY; ++j)
		{
			double mean = 0;
			int count = 0;
			for (int k = 0; k < numZ; ++k)
			{
				if(numTimesSeen[i](j,k) == 0)
					continue;
				mean += (double)numTimesSeen[i](j,k);
				count++;
			}
			if(count == 0)
				continue;
			mean = mean/(double)count;
			collapsedMean[j][i] = mean;
		}
	}
	const string imageName = outputFolder + "DUC_freeSpace_" + scanNumber + ".png";
	displayCollapsed(collapsedMean, numX, numY, imageName);
	
	
}

void displayCollapsed(const vector<VectorXi> & numTimesSeen, 
	const int numX, const int numY,
	const string & imageName){
	float average, sigma;
	average, sigma = 0;
	size_t count = 0;
	for(auto & v : numTimesSeen){
		for(int i = 0; i < v.size(); ++i){
			if(*(v.data() + i) == 0)
				continue;
			average += *(v.data() + i);
			count++;
		}
	}
	average = average/count;

	for(auto & v : numTimesSeen){
		for(int i = 0; i < v.size(); ++i){
			if(*(v.data() + i)!=0)
				sigma += (*(v.data() + i)-average)*( *(v.data() + i)-average);
		}
	}
	sigma = sigma/(count - 1);
	sigma = sqrt(sigma);

	Mat collapsedMap (numY, numX, CV_8UC3, Scalar::all(255));

	for (int j = 0; j < collapsedMap.rows; ++j)
	{
		uchar * dst = collapsedMap.ptr<uchar>(j);
		
		for (int i = 0; i < collapsedMap.cols; ++i)
		{
			if(numTimesSeen[j][i] != 0){
				const int gray = max(0, min(255,
					 static_cast<int>(255.0 * (numTimesSeen[j][i]
					  - average) / ((3 * sigma) + 1.0) / 2.0)));
				int red, green, blue;
				if (gray < 128) {
					red = 0;
					blue = 2 * gray;
					green = 255 - blue;
				} else {
					blue = 0;
					red = 2 * (gray - 128);
					green = 255 - red;
				}
				dst[i*3] = blue;
				dst[i*3 +1] = green;
				dst[i*3 + 2] = red;
			}
		} 
	}

	imshow("Preview", collapsedMap);
	waitKey(500); 

	imwrite(imageName, collapsedMap);
}