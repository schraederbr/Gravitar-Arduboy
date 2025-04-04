#include <Arduboy2.h>
#include <math.h>
#include "Font3x5.h"
#include <FixedPoints.h>
#include <FixedPointsCommon.h>
Arduboy2 arduboy;
Font3x5 font3x5 = Font3x5();
//This probably uses 32 bit math so we might as well boost it up
using FP = SFixed<11,20>;

//Frame count since starting life
int frames_alive = 0;

// -----------------------
// World & Camera Settings
// -----------------------
const int screenWidth  = 128;
const int screenHeight = 64;
const int planetMinRadius = 40;
const int planetMaxRadius = 150;
const int planetStepAngle = 25;

const int worldWidth  = 512;
const int worldHeight = 512;

// Recalculate the world center
const int worldCenterX = worldWidth  / 2; 
const int worldCenterY = worldHeight / 2; 

// Camera variables (centered on the ship)
float cameraX = 0;
float cameraY = 0;

const int TURRET_ACTIVE_DISTANCE = 60;
const int TURRET_SCORE = 250;
const int FRAME_RATE = 60;
const int DEFAULT_LIVES = 3;
int lives = DEFAULT_LIVES;
const int NUM_FUEL_PICKUPS = 3;
const int DEFAULT_FUEL = 20000;
const int FUEL_PER_PICKUP = 2500;
const int FUEL_PICKUP_SCORE = 100;
const int THRUST_FUEL_BURN_RATE = 1000; //per second when thrusting
const int TRACTOR_FUEL_BURN_RATE = 2000; //extra fuel burned per second when tractor beam is active
int currentFuel = DEFAULT_FUEL;
int score = 0;
float startX = worldWidth / 2;
float startY = 256 - (planetMaxRadius + 10);  
float shipX, shipY;  // Position of the ship in "world" coordinates
float shipAngle;      // Ship’s facing angle in radians
float velX, velY;     // Velocity

const float ACCELERATION   = 0.02f;  // How fast the ship accelerates
const float ROTATION_SPEED = 0.1f;  // Radians/frame rotation
const float FRICTION       = 0.997f; // Slows the ship gradually
const float GRAVITY_ACCEL = 0.005f;  // Adjust this constant to change gravity strength.


const float turretWidth = 3.0;
const float turretHeight = 6.0;
struct FuelPickup {
    float x;
    float y;
    float angle;
    int fuelAmount = FUEL_PER_PICKUP;
};

FuelPickup fuelPickups[NUM_FUEL_PICKUPS];
int pickupCount = 0;
// -----------------------
// Circle / Background
// -----------------------
struct Point2D {
  float x;
  float y;
};



// Example data structure for our polygon points in fixed-point:
struct FixedPoint2D {
  FP x;
  FP y;
};

struct Turret {
  float x;
  float y;
  float angle;
  int   fireTimer;  // counts down; when <= 0, shoot
  //Points for each corner
  Point2D p1;
  Point2D p2;
  Point2D p3;
  Point2D p4;
};
// Point2D* circle_points = nullptr;
const int CIRCLE_NUM_POINTS  = (int)(360 / planetStepAngle);
static Point2D circle_points[CIRCLE_NUM_POINTS];

static FixedPoint2D fixed_points[CIRCLE_NUM_POINTS];
//Might want these to be float
int circleCenterX = 0;
int circleCenterY = 0;
const int MAX_TURRETS = 5; // how many stars you want
bool gameOver = false;

// Simple bullet structure
struct Bullet {
  bool active;  // Is this bullet slot in use?
  float x;      // World position
  float y;      // World position
  float vx;     // Velocity X
  float vy;     // Velocity Y
  int framesAlive = 0;
};

// Turret bullet data
static const int MAX_TURRET_BULLETS = 15;
static const int MAX_TURRET_BULLET_FRAMES_ALIVE = 120;


Bullet turretBullets[MAX_TURRET_BULLETS];

// Each turret also needs a timer for shooting, say it fires every N frames
// We'll store this in your Turret struct, or a parallel array of timers.
static const int TURRET_FIRE_DELAY = 90; 
//Frames before turrets start firing when you start the game. 
static const int TURRET_START_DELAY = 180;


Turret turrets[MAX_TURRETS];
int turretCount = 0;


// Maximum number of on-screen bullets
static const int MAX_BULLETS = 5;
static const float MAX_BULLET_FRAMES_ALIVE = 120;


// The global array of bullets
Bullet bullets[MAX_BULLETS];

void spawnBullet(float x, float y, float angle) {
  // Look for an inactive bullet slot
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (!bullets[i].active) {
      // Activate and position it
      bullets[i].active = true;
      bullets[i].x      = x;
      bullets[i].y      = y;

      // Decide bullet speed
      float bulletSpeed = 1.0f;  // tweak as you wish

      // We want bullet direction to match the ship’s facing
      // Remember your code does "shipAngle = 0 => up," so we use (angle - PI/2)
      bullets[i].vx = cos(angle - PI / 2) * bulletSpeed;
      bullets[i].vy = sin(angle - PI / 2) * bulletSpeed;

      // Stop after spawning 1 bullet
      break;
    }
  }
}


void spawnTurretBullet(float x, float y, float targetX, float targetY) {
  //Do short delay so player doesn't get immediately shot
  if(frames_alive > TURRET_START_DELAY){
    // Find an inactive slot
    for (int i = 0; i < MAX_TURRET_BULLETS; i++) {
      if (!turretBullets[i].active) {
        turretBullets[i].active = true;

        // Start at turret position
        turretBullets[i].x = x;
        turretBullets[i].y = y;

        // Aim at (targetX, targetY)
        float dx = targetX - x;
        float dy = targetY - y;
        float length = sqrt(dx*dx + dy*dy);
        if (length < 0.001f) {
          length = 0.001f; // avoid divide by zero
        }

        float speed = 0.5f; // adjust bullet speed as desired
        turretBullets[i].vx = (dx / length) * speed;
        turretBullets[i].vy = (dy / length) * speed;

        // Done
        break;
      }
    }
  }
}




// Returns a random float in [min_val, max_val].
float randomFloat(float min_val, float max_val) {
  return min_val + (max_val - min_val) * (random(0, 10001) / 10000.0f);
}

Point2D randomPointOnLine(const Point2D& p0, const Point2D& p1) {
    // Generate a random t in [0,1]. On Arduino, random(max) returns a long in [0, max).
    // Adjust the range to get a floating-point number in [0,1].
    float t = static_cast<float>(random(10000)) / 10000.0f;

    // Linearly interpolate
    Point2D result;
    result.x = p0.x + t * (p1.x - p0.x);
    result.y = p0.y + t * (p1.y - p0.y);

    return result;
}

bool pointInTriangle(float px, float py, float x1, float y1, float x2, float y2, float x3, float y3) {
    // Compute the area of the triangle using the determinant formula
    float denominator = ((y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3));

    // Avoid division by zero (if the triangle is degenerate)
    if (denominator == 0.0) return false;

    // Compute barycentric coordinates
    float alpha = ((y2 - y3) * (px - x3) + (x3 - x2) * (py - y3)) / denominator;
    float beta  = ((y3 - y1) * (px - x3) + (x1 - x3) * (py - y3)) / denominator;
    float gamma = 1.0 - alpha - beta;

    // Check if the point is inside the triangle (all barycentric coordinates between 0 and 1)
    return (alpha >= 0.0 && alpha <= 1.0) && 
           (beta  >= 0.0 && beta  <= 1.0) && 
           (gamma >= 0.0 && gamma <= 1.0);
}

bool pointInRectangle(float px, float py, Turret* turret){
  return pointInRectangle(px, py, 
                          turret->p1.x, turret->p1.y, 
                          turret->p2.x, turret->p2.y, 
                          turret->p3.x, turret->p3.y, 
                          turret->p4.x, turret->p4.y);
}
bool pointInRectangle(
    float px, float py,
    float x1, float y1,
    float x2, float y2,
    float x3, float y3,
    float x4, float y4)
{
    // Check if point is in the triangle (x1,y1), (x2,y2), (x3,y3)
    bool inFirstTriangle = pointInTriangle(px, py, x1, y1, x2, y2, x3, y3);

    // Check if point is in the triangle (x1,y1), (x3,y3), (x4,y4)
    bool inSecondTriangle = pointInTriangle(px, py, x1, y1, x3, y3, x4, y4);

    return (inFirstTriangle || inSecondTriangle);
}

float getDistanceSquared(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return dx * dx + dy * dy;
}

bool isWithinDistance(float x1, float y1, float x2, float y2, float distance) {
    return getDistanceSquared(x1, y1, x2, y2) <= distance * distance;
}

// pointInPlanet using fixed-point arithmetic
bool fixedPointInPolygon(int numPoints, const FixedPoint2D points[], FP px, FP py) {
  // Serial.println(F("Entering pointInPlanet (fixed-point)"));

  bool inside = false;

  // Print the point we are checking (cast to float for printing)
//   Serial.print(F("  Checking point ("));
//   Serial.print((float)px);
//   Serial.print(F(", "));
//   Serial.print((float)py);
//   Serial.println(F(")"));

  int j = numPoints - 1;
  for (int i = 0; i < numPoints; i++) {
    // Add offsets in fixed point
    FP xi = points[i].x + worldCenterX;
    FP yi = points[i].y + worldCenterY;
    FP xj = points[j].x + worldCenterX;
    FP yj = points[j].y + worldCenterY;

    // Serial.print(F("  Edge from ("));
    // Serial.print((float)xi);
    // Serial.print(F(", "));
    // Serial.print((float)yi);
    // Serial.print(F(") to ("));
    // Serial.print((float)xj);
    // Serial.print(F(", "));
    // Serial.print((float)yj);
    // Serial.println(F(")"));

    // Equivalent of the original check, but all in fixed-point:
    //   if (((yi > py) != (yj > py)) &&
    //       (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) { ... }

    // Check if the ray crosses the edge
    bool differentSides = ((yi > py) != (yj > py));
    if (differentSides) {
      // Compute the intersection's X coordinate in fixed point
      FP intersectionX = (xj - xi) * (py - yi) / (yj - yi) + xi;

      if (px < intersectionX) {
        inside = !inside;
        // Serial.print(F("    Ray crosses edge, toggling inside -> "));
        // Serial.println(inside ? F("true") : F("false"));
      }
    }

    j = i;
  }

//   Serial.print(F("Leaving pointInPlanet, final inside = "));
//   Serial.println(inside ? F("true") : F("false"));
  return inside;
}

bool pointInPlanet(int numPoints, float px, float py) {
  FP pxFP(px);
  FP pyFP(py);
  return fixedPointInPolygon(numPoints, fixed_points, pxFP, pyFP);
}

// // The pointInPlanet function with serial prints at each major step
// bool pointInPlanet(int numPoints, const Point2D points[], float px, float py) {
//   Serial.println(F("Entering pointInPlanet"));

//   bool inside = false;
//   int pxInt = (int)px;
//   int pyInt = (int)py;

//   // Print the point we are checking
//   Serial.print(F("  Checking point ("));
//   Serial.print(pxInt);
//   Serial.print(F(", "));
//   Serial.print(pyInt);
//   Serial.println(F(")"));

//   int j = numPoints - 1;
//   for (int i = 0; i < numPoints; i++) {
//     int xi = (int)points[i].x + worldCenterX;
//     int yi = (int)points[i].y + worldCenterY;
//     int xj = (int)points[j].x + worldCenterX;
//     int yj = (int)points[j].y + worldCenterY;

//     Serial.print(F("  Edge from ("));
//     Serial.print(xi);
//     Serial.print(F(", "));
//     Serial.print(yi);
//     Serial.print(F(") to ("));
//     Serial.print(xj);
//     Serial.print(F(", "));
//     Serial.print(yj);
//     Serial.println(F(")"));

//     // Check if the ray crosses the edge
//     bool crosses = ((yi > pyInt) != (yj > pyInt)) &&
//                    (pxInt < (xj - xi) * (pyInt - yi) / (float)(yj - yi) + xi);

//     if (crosses) {
//       inside = !inside;
//       Serial.print(F("    Ray crosses edge, toggling inside -> "));
//       Serial.println(inside ? F("true") : F("false"));
//     }

//     j = i;
//   }

//   Serial.print(F("Leaving pointInPlanet, final inside = "));
//   Serial.println(inside ? F("true") : F("false"));
//   return inside;
// }


// // Returns true if the point (px,py) is inside the polygon
// bool pointInPlanet(int numPoints, const Point2D points[], float px, float py) {
//   bool inside = false;
//   int pxInt = (int)px;
//   int pyInt = (int)py;
//   // The planet is defined by circle_points.
//   // Each point in circle_points is relative to (0,0); the actual world position is offset by worldCenterX/Y.
//   int j = numPoints - 1;
//   for (int i = 0; i < numPoints; i++) {
//     int xi = (int)points[i].x + worldCenterX;
//     int yi = (int)points[i].y + worldCenterY;
//     int xj = (int)points[j].x + worldCenterX;
//     int yj = (int)points[j].y + worldCenterY;
//     // Check if the ray crosses the edge
//     if (((yi > pyInt) != (yj > pyInt)) &&
//          (pxInt < (xj - xi) * (pyInt - yi) / (yj - yi) + xi)) {
//       inside = !inside;
//     }
//     j = i;
//   }
//   return inside;
// }
