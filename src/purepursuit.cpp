#include "purepursuit.h"

std::vector<Waypoint> path = {{1, 1}, {100, 100}, {300, 50}, {500, 200}};

void pathFollowNormal(std::vector<Waypoint> pathToFollow) {

}

void chasePoint(Waypoint P) {
    //20, 1
    //100 -> 12000 = (x120)
    float kP_linear = 240;
    float kP_angular = 24;

    do {
        float linErr = distance(getRobotPose(), P);
        float angErr = angle(getRobotPose(), P);

        LeftDT.move_voltage(0.7*(linErr * kP_linear - angErr * kP_angular));
        RightDT.move_voltage(0.7*(linErr * kP_linear + angErr * kP_angular));
    }while(!robotSettled(P));
    controller.rumble("..");
}

void pathFollowPurePursuit(std::vector<Waypoint> pathToFollow, float maximumVel, float maximumA, float constantK) {
    std::vector<Waypoint> path = pathGen(pathToFollow, maximumVel, maximumA, constantK);


}

std::vector<Waypoint> pathGen(std::vector<Waypoint> pathToFollow, float maxVel, float maxA, float velocityK) {
    //Following DAWGMA Document

    //Step 1. Injecting extra points
    int inchSpacing = 6;
    std::vector<Waypoint> newPath;
    for(int lineSeg = 0; lineSeg < pathToFollow.size()-1; lineSeg++) {
        Waypoint dirVector = getDirVector(pathToFollow[lineSeg], pathToFollow[lineSeg+1]); 
        int totalPointsFit = (int) (getLength(dirVector) / inchSpacing);
        dirVector = scalarMult(normalizeVect(dirVector), inchSpacing);

        //Inject points
        for(int i=0; i<totalPointsFit; i++) {
            newPath.push_back(Waypoint(pathToFollow[lineSeg].getX() + scalarMult(dirVector, i).getX(), pathToFollow[lineSeg].getY() + scalarMult(dirVector, i).getY()));
        }
    }
    newPath.push_back(Waypoint(pathToFollow[pathToFollow.size()-1].getX(), pathToFollow[pathToFollow.size()-1].getY()));

    //Step 2. Smooth Path
    newPath = smooth(newPath, 0.3, 0.7, 0.001);

    //Step 3. Distance between points
    newPath[0].setDist(0);
    for(int i=1; i<newPath.size(); i++) {
        //Runing Sum (D_i = D_i-1 + dist(D_i, D_i-1)
        newPath[i].setDist((newPath[i-1].getDist() + distance(newPath[i], newPath[i-1])));
    }

    //Step 4. Calculate curvature (1/radius) between points
    newPath[0].setCurv(0);
    for(int i=1; i<(newPath.size()-1); i++) {
        float x1 = newPath[i-1].getX();
        float y1 = newPath[i-1].getY();
        float x2 = newPath[i].getX();
        float y2 = newPath[i].getY();
        float x3 = newPath[i+1].getX();
        float y3 = newPath[i+1].getY();

        if(x1 == y1) {
            //Account for divide by 0 edge cases
            newPath[i-1].setX(x1 + 0.001);
        }

        float kOne = 0.5 * (pow(x1, 2) + pow(y1, 2) - pow(x2, 2) - pow(y2, 2)) / (x1 - x2);
        float kTwo = (y1 - y2) / (x1 - x2);

        float b = 0.5 * (pow(x2, 2) - 2 * x2 * kOne + pow(y2, 2) - pow(x3, 2) + 2 * x3 * kOne - pow(y3, 2)) / (x3 * kTwo - y3 + y2 - x2 * kTwo);
        float a = kOne - kTwo * b;

        float r = std::sqrt(pow((x1 - a), 2) + pow((y1 - b), 2));
        float c = 1/r;

        if(std::isnan(c)) {
            //Straight line
            newPath[i].setCurv(0);
        }
        else {
            newPath[i].setCurv(c);
        }
    }
    newPath[newPath.size()-1].setCurv(0);

    //Step 5a. Calculate Velocities
    for(int i=0; i<newPath.size(); i++) {
        if(newPath[i].getCurv() == 0) {
            newPath[i].setVel(maxVel);
        }
        else {
            newPath[i].setVel(std::min(velocityK/(newPath[i].getCurv()), maxVel));
        }
    }

    //Step 5b & c
    newPath[newPath.size()-1].setVel(0);
    for(int i=newPath.size()-1; i>0; i--) {
        float dist = distance(newPath[i], newPath[i-1]);
        float newVel = std::sqrt(pow(newPath[i].getVel(), 2) + 2 * maxA * dist);
        newPath[i].setVel(std::min(newPath[i-1].getVel(), newVel));
    }


    return newPath;
}

std::vector<Waypoint> smooth(std::vector<Waypoint> roughPath, float a, float b, float tolerance) {
    //a == weight on the data, b == weight for smoothing"
    //A larger "b" value will result in a smoother path, be careful!
    std::vector<Waypoint> smoothPath;
    for(int index = 0; index < roughPath.size(); index++) {
        smoothPath.push_back(roughPath[index]);
    }
    
    double change = tolerance;
	while(change >= tolerance) {
		change = 0.0;
		for(int i=1; i<roughPath.size()-1; i++) {
            double tempX = smoothPath[i].getX();
            smoothPath[i].setX(smoothPath[i].getX() + (a * (roughPath[i].getX() - smoothPath[i].getX()) + b * (smoothPath[i-1].getX() + smoothPath[i+1].getX() - (2.0 * smoothPath[i].getX()))));
            change += abs(tempX - smoothPath[i].getX());

            double tempY = smoothPath[i].getY();
            smoothPath[i].setY(smoothPath[i].getY() + (a * (roughPath[i].getY() - smoothPath[i].getY()) + b * (smoothPath[i-1].getY() + smoothPath[i+1].getY() - (2.0 * smoothPath[i].getY()))));
            change += abs(tempY - smoothPath[i].getY());
		}
	}

    return smoothPath;
    //Credit ~Team 2168 FRC/FTC for smoothing algorithm
}

int findClosestPoint(Waypoint P, std::vector<Waypoint> path) {
    //Returns index of closest point in path to point P
    float smallestDist = 10000000;
    int smallestIndex = -1;

    float dist = smallestDist;
    for(int i=0; i<path.size(); i++) {
        dist = distance(P, path[i]);
        if(dist < smallestDist) {
            smallestDist = dist;
            smallestIndex = i;
        }
    }

    return smallestIndex;
}

float circleLineIntersect(Waypoint start, Waypoint end, Waypoint curPos, float lookaheadRadius) {
    //Returns t value of intersection between Circle and Line Segment [formed by "start" & "end"] (-1 if none)
    Waypoint dirVect = getDirVector(start, end);
    Waypoint centerVect = getDirVector(curPos, start);

    float a = dotProduct(dirVect, dirVect);
    float b = 2 * dotProduct(centerVect, dirVect);
    float c = dotProduct(centerVect, centerVect) - lookaheadRadius * lookaheadRadius;

    //Calculate discriminant: b^2-4ac
    float d = b * b - 4 * a * c;

    if(d >= 0) {
        d = std::sqrt(d);
        float t1 = (-b - d) / (2 * a);
        float t2 = (-b + d) / (2 * a);
        if(t1 >= 0 && t1 <= 1) {
            return t1;
        }
        else if(t2 >= 0 && t2 <= 1) {
            return t2;
        }
        else {
            return -1;
        }
    }
    else {
        return -1;
    }


   //https://stackoverflow.com/questions/1073336/circle-line-segment-collision-detection-algorithm/1084899#1084899
}

Waypoint findLookaheadPoint(std::vector<Waypoint> pathToFollow, Waypoint curPos, Waypoint prevLookAhead, int prevLookAheadIndex, float lookaheadRadius) {
    Waypoint curLookAhead = prevLookAhead;

    float t;
    float fracIndex;

    for(int i=0; i<pathToFollow.size()-1; i++) {
        t = circleLineIntersect(pathToFollow[i], pathToFollow[i+1], curPos, lookaheadRadius);
        
        //If valid point
        if((t >= 0) && (t <= 1)) {
            //TODO: Optimize by starting at last lookahead point index
            
            fracIndex = i + t;
            if(fracIndex > prevLookAheadIndex) {
                curLookAhead = Waypoint(pathToFollow[i].getX() + t * (pathToFollow[i+1].getX() - pathToFollow[i].getX()), pathToFollow[i].getY() + t * (pathToFollow[i+1].getY() - pathToFollow[i].getY()));
            }
        }
    }

    return curLookAhead;
}

float getSignedCurvature(Waypoint curPos, Waypoint lookAhead, float orientation) {
    //Signed Curvature = curvature * side

    //Curvature = 2x/L^2 (use DAWGMA document)
    float a = -std::tan(curPos.getTheta());
    float b = 1;
    float c = std::tan(curPos.getTheta()) * curPos.getX() - curPos.getY();
    float L = std::hypot(lookAhead.getX()-curPos.getX(), lookAhead.getY()-curPos.getY());

    //x = |ax+by+c| / sqrt(a^2+b^2), b = 1
    //x = |ax + y + c| / sqrt(a^2 + 1)
    float x = std::fabs(a * lookAhead.getX() + lookAhead.getY() + c) / std::sqrt(pow(a, 2) + 1);
    float curvature = ((2 * x) / pow(L, 2));

    float side = numbersign(std::sin(curPos.getTheta()) * (lookAhead.getX()-curPos.getX()) - std::cos(curPos.getTheta()) * (lookAhead.getY()-curPos.getY()));

    return curvature * side;
}
