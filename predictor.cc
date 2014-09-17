#include "predictor.h"
#include <math.h>

#define PHT_CTR_MAX  3
#define PHT_CTR_INIT 2

// Knobs for perceptron predictor
#define HIST_LEN 31
#define NUM_PERCEPTRON_TABLES 2
#define PERCEPTRON_INDEX_BITS 9 // 2^PERCEPTRON_INDEX_BITS = no. of PERCEPTRON_ENTRIES
#define WEIGHT_BITS 8


/////////////// STORAGE BUDGET JUSTIFICATION ////////////////
// Total storage budget: 32KB + 1024 bits
// No. of perceptron tables: 2
// One Perceptron table size: 2 ^ PERCEPTRON_INDEX_BITS = 2^9=512 entries
// Weight counter size in one perceptron table: 512 * 32 * 8 bits = 2 ^ 17 Kbits or 16 KB
// Total size of two perceptron tables: 32 KB
// GHR size: 32 bits
// Total Size = total perceptron table size + GHR size
/////////////////////////////////////////////////////////////



/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

PREDICTOR::PREDICTOR(void){

  ghr              = 0xFFFFFFFF;
  numPerceptronEntries = (1<< PERCEPTRON_INDEX_BITS);
  trainingThreshold = 1.93 * HIST_LEN + 14; // optimum value
  tc=0;
  sum1=0;
  sum2=0;

  perceptronTable = new INT32**[NUM_PERCEPTRON_TABLES];
  
  for(int i=0;i<NUM_PERCEPTRON_TABLES;i++)
  {
	  perceptronTable[i]= new INT32*[numPerceptronEntries];
    for (uint j=0; j<numPerceptronEntries; j++)
     {
	  perceptronTable[i][j]= new INT32[HIST_LEN+1];  // for the extra weight w0
     }
  }

  // Initializing the table entries
   for(int i=0;i<NUM_PERCEPTRON_TABLES;i++)
  {
    for (uint j=0; j<numPerceptronEntries; j++)
     {
       for(int k=0;k<HIST_LEN+1;k++)
      {
	perceptronTable[i][j][k]= 0;  
      }
     }
  }
}

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

bool   PREDICTOR::GetPrediction(UINT32 PC){
  
  UINT32 pc_hash=0,temp_PC1=0,temp_PC2=0, tempghr;
  INT32 *perceptron_entry, sum;
  
  UINT32 numShift;	//no. of shifts to GHR to get the branch histories
  
  // Indexing into the first perceptron table using folded PC hash

  temp_PC1=PC>> PERCEPTRON_INDEX_BITS;
  temp_PC2=temp_PC1>> PERCEPTRON_INDEX_BITS;
  pc_hash=(PC % numPerceptronEntries) ^ (temp_PC1 % numPerceptronEntries) ^ (temp_PC2 % numPerceptronEntries); // XORing LSB bits of PC with higher order bits of PC to get pc_hash
  perceptron_entry = perceptronTable[0][pc_hash];

  sum1 = 1 * perceptron_entry[0]; // Bias weight = 1 always

  for(UINT32 i=0; i<HIST_LEN; i++){

	  numShift=i;
	tempghr=ghr>>numShift;

    if(tempghr % 2) // if xi=1 => Taken branch, so add it to the sum. Else subtract it from the sum
	   sum1+= perceptron_entry[i+1];
	else
	   sum1-= perceptron_entry[i+1];
  }


    // Indexing into the second perceptron table using PC XOR GHR

    pc_hash=(PC ^ ghr) % numPerceptronEntries;

    perceptron_entry = perceptronTable[1][pc_hash];
    sum2 = 1 * perceptron_entry[1]; // Bias weight = 1 always

    for(UINT32 i=0; i<HIST_LEN; i++){

  	  numShift=i;
  	  tempghr=ghr>>numShift;

      if(tempghr % 2) // if xi=1 => Taken branch, so add it to the sum. Else subtract it from the sum
  	   sum2+= perceptron_entry[i+1];
  	else
  	   sum2-= perceptron_entry[i+1];
    }


    // Calculate the final sum

    sum=sum1+sum2;

	if(sum >= 0)
	return TAKEN;
	else
	return NOT_TAKEN;

  
}


/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

void  PREDICTOR::UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget){
  
  UINT32 pc_hash=0,temp_PC1=0,temp_PC2=0,tempghr;
  INT32 *perceptron_entry,sum;
  INT32 t;
  
  UINT32 numShift;

  if(resolveDir)
 	t=1;
  else
 	t=-1;

  // Indexing into first perceptron table

  temp_PC1=PC>> PERCEPTRON_INDEX_BITS;
  temp_PC2=temp_PC1>> PERCEPTRON_INDEX_BITS;
  pc_hash=(PC % numPerceptronEntries) ^ (temp_PC1 % numPerceptronEntries) ^ (temp_PC2 % numPerceptronEntries); // XORing LSB bits of PC with higher order bits of PC to get pc_hash

  perceptron_entry = perceptronTable[0][pc_hash];
  
  sum=sum1+sum2;

  
  if(resolveDir != predDir || abs(sum) <= trainingThreshold)
  {
	// Update the first weight based on x0 which is always 1
	  if(t==1)
	  {
		if(perceptron_entry[0]< (pow(2,WEIGHT_BITS-1)-1))
		   perceptron_entry[0] = perceptron_entry[0] + t;
	  }
	  else if(t==-1)
	  {
		if(perceptron_entry[0]> (-1 * pow(2,WEIGHT_BITS-1)))
		   perceptron_entry[0] = perceptron_entry[0] + t;
	  }

	
    for(UINT32 i=0; i< HIST_LEN; i++)
	{
	  // Update weights of the remaining perceptron entries
      tempghr=ghr>>numShift;
	  numShift=i;

      if((tempghr % 2) == resolveDir)
      {    // saturate incremement the weight
    	  if(perceptron_entry[i]<(pow(2,WEIGHT_BITS-1)-1))
    		  perceptron_entry[i]+=1;
      }

	  else
	  {    // saturate decremement the weight
    	  if(perceptron_entry[i]>(-1 * pow(2,WEIGHT_BITS-1)))
    		  perceptron_entry[i]-=1;
	  }

	}
   }
   
  // Indexing into the second perceptron table

  pc_hash=(PC ^ ghr) % numPerceptronEntries;
  perceptron_entry = perceptronTable[1][pc_hash];

  if(resolveDir != predDir || abs(sum) <= trainingThreshold)
  {
  	// Update the first weight based on x0 which is always 1
  	  if(t==1)
  	  {
  		if(perceptron_entry[0]<(pow(2,WEIGHT_BITS-1)-1))
  		   perceptron_entry[0] = perceptron_entry[0] + t;
  	  }
  	  else if(t==-1)
  	  {
  		if(perceptron_entry[0]>(-1 * pow(2,WEIGHT_BITS-1)))
  		   perceptron_entry[0] = perceptron_entry[0] + t;
  	  }


      for(UINT32 i=0; i< HIST_LEN; i++)
  	  {
  	  // Update weights of the remaining perceptron entries
        tempghr=ghr>>numShift;
  	    numShift=i;

        if((tempghr % 2) == resolveDir)
        {    // saturate incremement the weight
      	  if(perceptron_entry[i]<(pow(2,WEIGHT_BITS-1)-1))
      		  perceptron_entry[i]+=1;
        }

        else
        {    // saturate decremement the weight
      	  if(perceptron_entry[i]>(-1 * pow(2,WEIGHT_BITS-1)))
      		  perceptron_entry[i]-=1;
        }

  	   }
     }

   
   // update the GHR

  ghr = (ghr << 1);

  if(resolveDir == TAKEN){
    ghr++; 
  }
  
  // Update the threshold

  if(predDir != resolveDir)
  {
	  tc += 1;
	  if(tc==127)
	  {
		  trainingThreshold+=1;
		  tc=0;
	  }

  }

  if(predDir == resolveDir && abs(sum) <= trainingThreshold)
  {
  	  tc -= 1;
  	  if(tc==-128)
  	  {
  		  trainingThreshold-=1;
  		  tc=0;
  	  }

  }

  // Reset sum1 and sum2 for the next branch

  sum1=0;
  sum2=0;

}

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

void    PREDICTOR::TrackOtherInst(UINT32 PC, OpType opType, UINT32 branchTarget){

  // This function is called for instructions which are not
  // conditional branches, just in case someone decides to design
  // a predictor that uses information from such instructions.
  // We expect most contestants to leave this function untouched.

  return;
}
