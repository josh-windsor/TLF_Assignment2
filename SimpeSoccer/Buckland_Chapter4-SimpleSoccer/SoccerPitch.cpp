#include "SoccerPitch.h"
#include "SoccerBall.h"
#include "Referee.h"
#include "RefereeStates.h"
#include "Goal.h"
#include "Common/Game/Region.h"
#include "Common/2D/Transformations.h"
#include "Common/2D/Geometry.h"
#include "Common/Debug/DebugConsole.h"
#include "Common/Game/EntityManager.h"
#include "GlobalParamLoader.h"
#include "PlayerBase.h"
#include "TeamStates.h"
#include "Common/misc/FrameCounter.h"
#include "conio.h"

const int NumRegionsHorizontal = 6; 
const int NumRegionsVertical   = 3;

//------------------------------- ctor -----------------------------------
//------------------------------------------------------------------------
SoccerPitch::SoccerPitch(int cx, int cy):m_cxClient(cx),
										 m_cyClient(cy),
										 m_bPaused(false),
										 m_bGoalKeeperHasBall(false),
										 m_Regions(NumRegionsHorizontal*NumRegionsVertical),
										 m_bGameOn(true)
{
  //define the playing area
  m_pPlayingArea = new Region(20, 20, cx-20, cy-20);

  //create the regions  
  CreateRegions(PlayingArea()->Width() / (double)NumRegionsHorizontal,
				PlayingArea()->Height() / (double)NumRegionsVertical);

  //create the goals
   m_pRedGoal  = new Goal(Vector2D( m_pPlayingArea->Left(), (cy- Prm.GoalWidth)/2),
						  Vector2D(m_pPlayingArea->Left(), cy - (cy- Prm.GoalWidth)/2),
						  Vector2D(1,0));
   


  m_pBlueGoal = new Goal( Vector2D( m_pPlayingArea->Right(), (cy- Prm.GoalWidth)/2),
						  Vector2D(m_pPlayingArea->Right(), cy - (cy- Prm.GoalWidth)/2),
						  Vector2D(-1,0));


  //create the soccer ball
  m_pBall = new SoccerBall(Vector2D((double)m_cxClient/2.0, (double)m_cyClient/2.0),
						   Prm.BallSize,
						   Prm.BallMass,
						   m_vecWalls);

  //create the teams 
  difficultyFiles[0] = new AIParamLoader("ParamAEasy.ini");
  difficultyFiles[1] = new AIParamLoader("ParamAMedium.ini");
  difficultyFiles[2] = new AIParamLoader("ParamAHard.ini");
  difficultyFiles[3] = new AIParamLoader("ParamBEasy.ini");
  difficultyFiles[4] = new AIParamLoader("ParamBMedium.ini");
  difficultyFiles[5] = new AIParamLoader("ParamBHard.ini");
  teamDifficultyFiles[0] = new AIParamTeamLoader("ParamATeamEasy.ini");
  teamDifficultyFiles[1] = new AIParamTeamLoader("ParamATeamMedium.ini");
  teamDifficultyFiles[2] = new AIParamTeamLoader("ParamATeamHard.ini");
  teamDifficultyFiles[3] = new AIParamTeamLoader("ParamBTeamEasy.ini");
  teamDifficultyFiles[4] = new AIParamTeamLoader("ParamBTeamMedium.ini");
  teamDifficultyFiles[5] = new AIParamTeamLoader("ParamBTeamHard.ini");
  m_pRedTeam  = new SoccerTeam(m_pRedGoal, m_pBlueGoal, this, SoccerTeam::red, difficultyFiles[1], teamDifficultyFiles[1]);
  m_pBlueTeam = new SoccerTeam(m_pBlueGoal, m_pRedGoal, this, SoccerTeam::blue, difficultyFiles[4], teamDifficultyFiles[4]);

  //make sure each team knows who their opponents are
  m_pRedTeam->SetOpponents(m_pBlueTeam);
  m_pBlueTeam->SetOpponents(m_pRedTeam); 

  //create the walls
  Vector2D TopLeft(m_pPlayingArea->Left(), m_pPlayingArea->Top());                                        
  Vector2D TopRight(m_pPlayingArea->Right(), m_pPlayingArea->Top());
  Vector2D BottomRight(m_pPlayingArea->Right(), m_pPlayingArea->Bottom());
  Vector2D BottomLeft(m_pPlayingArea->Left(), m_pPlayingArea->Bottom());
									  
  m_vecWalls.push_back(Wall2D(BottomLeft, m_pRedGoal->RightPost()));
  m_vecWalls.push_back(Wall2D(m_pRedGoal->LeftPost(), TopLeft));
  m_vecWalls.push_back(Wall2D(TopLeft, TopRight));
  m_vecWalls.push_back(Wall2D(TopRight, m_pBlueGoal->LeftPost()));
  m_vecWalls.push_back(Wall2D(m_pBlueGoal->RightPost(), BottomRight));
  m_vecWalls.push_back(Wall2D(BottomRight, BottomLeft));

  GlobalParamLoader* p = GlobalParamLoader::Instance();


  m_pReferee = new Referee(GlobalRefereeState::Instance(),
	  Vector2D(0, -1),
	  Vector2D(0.0, 0.0),
	  3.0f,
	  1.0f,
	  1.6f,
	  0.4f,
	  1.0f,
	  Vector2D((double)m_cxClient / 2.0, (((double)m_cyClient / 2.0) + 100.0)),
	  this
  );





}

//-------------------------------- dtor ----------------------------------
//------------------------------------------------------------------------
SoccerPitch::~SoccerPitch()
{
  delete m_pBall;

  delete m_pRedTeam;
  delete m_pBlueTeam;

  delete m_pRedGoal;
  delete m_pBlueGoal;

  delete m_pPlayingArea;

  delete m_pReferee;

  for (unsigned int i=0; i<m_Regions.size(); ++i)
  {
	delete m_Regions[i];
  }
}

//----------------------------- Update -----------------------------------
//
//  this demo works on a fixed frame rate (60 by default) so we don't need
//  to pass a time_elapsed as a parameter to the game entities
//------------------------------------------------------------------------
void SoccerPitch::Update()
{
  if (m_bPaused) return;

  static int tick = 0;

  //update the balls
  m_pBall->Update();

  //update the teams
  m_pRedTeam->Update();
  m_pBlueTeam->Update();

  m_pReferee->Update();


  //if a goal has been detected reset the pitch ready for kickoff
  if (m_pBlueGoal->Scored(m_pBall) || m_pRedGoal->Scored(m_pBall))
  {
	m_bGameOn = false;
	
	//reset the ball                                                      
	m_pBall->PlaceAtPosition(Vector2D((double)m_cxClient/2.0, (double)m_cyClient/2.0));

	//get the teams ready for kickoff
	m_pRedTeam->GetFSM()->ChangeState(PrepareForKickOff::Instance());
	m_pBlueTeam->GetFSM()->ChangeState(PrepareForKickOff::Instance());
  }
}

//------------------------- CreateRegions --------------------------------
void SoccerPitch::CreateRegions(double width, double height)
{  
  //index into the vector
  int idx = m_Regions.size()-1;
	
  for (int col=0; col<NumRegionsHorizontal; ++col)
  {
	for (int row=0; row<NumRegionsVertical; ++row)
	{
	  m_Regions[idx--] = new Region(PlayingArea()->Left()+col*width,
								   PlayingArea()->Top()+row*height,
								   PlayingArea()->Left()+(col+1)*width,
								   PlayingArea()->Top()+(row+1)*height,
								   idx);
	}
  }
}


//------------------------------ Render ----------------------------------
//------------------------------------------------------------------------
bool SoccerPitch::Render()
{
  //draw the grass
  gdi->LightBlueBrush();
  gdi->LightBlueBrush();
  gdi->Rect(0,0,m_cxClient, m_cyClient);

  //render regions
  if (Prm.bRegions)
  {   
	for (unsigned int r=0; r<m_Regions.size(); ++r)
	{
	  m_Regions[r]->Render(true);
	}
  }
  
  //render the goals
  gdi->HollowBrush();
  gdi->RedPen();
  gdi->Rect(m_pPlayingArea->Left(), (m_cyClient-Prm.GoalWidth)/2, m_pPlayingArea->Left()+40, m_cyClient - (m_cyClient-Prm.GoalWidth)/2);

  gdi->BluePen();
  gdi->Rect(m_pPlayingArea->Right(), (m_cyClient-Prm.GoalWidth)/2, m_pPlayingArea->Right()-40, m_cyClient - (m_cyClient-Prm.GoalWidth)/2);
  
  //render the pitch markings
  gdi->WhitePen();
  gdi->Circle(m_pPlayingArea->Center(), m_pPlayingArea->Width() * 0.125);
  gdi->Line(m_pPlayingArea->Center().x, m_pPlayingArea->Top(), m_pPlayingArea->Center().x, m_pPlayingArea->Bottom());
  gdi->WhiteBrush();
  gdi->Circle(m_pPlayingArea->Center(), 2.0);


  //the ball
  gdi->WhitePen();
  gdi->WhiteBrush();
  m_pBall->Render();
  
  //Render the teams
  m_pRedTeam->Render();
  m_pBlueTeam->Render(); 

  m_pReferee->Render();


  //render the walls
  gdi->WhitePen();
  for (unsigned int w=0; w<m_vecWalls.size(); ++w)
  {
	m_vecWalls[w].Render();
  }

  //show the score
  gdi->TextColor(Cgdi::red);
  gdi->TextAtPos((m_cxClient/2)-50, m_cyClient-18, "Red: " + ttos(m_pBlueGoal->NumGoalsScored()));

  gdi->TextColor(Cgdi::blue);
  gdi->TextAtPos((m_cxClient/2)+10, m_cyClient-18, "Blue: " + ttos(m_pRedGoal->NumGoalsScored()));

  return true;  
}

void SoccerPitch::SetDifficulty(bool redTeam, int difficulty) 
{
	if (redTeam)
	{
		m_pRedTeam->difficultyLevel = difficulty;
		difficulty += 3;
		m_pRedTeam->m_pPlayerParamFile = difficultyFiles[difficulty];
		m_pRedTeam->m_pTeamParamFile = teamDifficultyFiles[difficulty];

	}
	else
	{
		m_pBlueTeam->difficultyLevel = difficulty;
		m_pBlueTeam->m_pPlayerParamFile = difficultyFiles[difficulty];
		m_pBlueTeam->m_pTeamParamFile = teamDifficultyFiles[difficulty];

	}
}







