#include "FieldPlayerStates.h"
#include "Common/Debug/DebugConsole.h"
#include "SoccerPitch.h"
#include "FieldPlayer.h"
#include "SteeringBehaviors.h"
#include "SoccerTeam.h"
#include "Goal.h"
#include "Common/2D/geometry.h"
#include "SoccerBall.h"
#include "AIParamLoader.h"
#include "GlobalParamLoader.h"
#include "Common/Messaging/Telegram.h"
#include "Common/Messaging/MessageDispatcher.h"
#include "SoccerMessages.h"

#include "Common/time/Regulator.h"


//uncomment below to send state info to the debug window
#define PLAYER_STATE_INFO_ON


//************************************************************************ Global state

GlobalPlayerState* GlobalPlayerState::Instance()
{
  static GlobalPlayerState instance;

  return &instance;
}


void GlobalPlayerState::Execute(FieldPlayer* player)                                     
{
  //if a player is in possession and close to the ball reduce his max speed
  if((player->BallWithinReceivingRange()) && (player->isControllingPlayer()))
  {
	player->SetMaxSpeed(player->Team()->m_pPlayerParamFile->PlayerMaxSpeedWithBall);
  }

  else
  {
	 player->SetMaxSpeed(player->Team()->m_pPlayerParamFile->PlayerMaxSpeedWithoutBall);
  }
	
}


bool GlobalPlayerState::OnMessage(FieldPlayer* player, const Telegram& telegram)
{
  switch(telegram.Msg)
  {
  case Msg_ReceiveBall:
	{
	  //set the target
	  player->Steering()->SetTarget(*(static_cast<Vector2D*>(telegram.ExtraInfo)));

	  //change state 
	  player->GetFSM()->ChangeState(ReceiveBall::Instance());

	  return true;
	}

	break;

  case Msg_SupportAttacker:
	{
	  //if already supporting just return
	  if (player->GetFSM()->isInState(*SupportAttacker::Instance()))
	  {
		return true;
	  }
	  
	  //set the target to be the best supporting position
	  player->Steering()->SetTarget(player->Team()->GetSupportSpot());

	  //change the state
	  player->GetFSM()->ChangeState(SupportAttacker::Instance());

	  return true;
	}

	break;

 case Msg_Wait:
	{
	  //change the state
	  player->GetFSM()->ChangeState(Wait::Instance());

	  return true;
	}

	break;

  case Msg_GoHome:
	{
	  player->SetDefaultHomeRegion();
	  
	  player->GetFSM()->ChangeState(ReturnToHomeRegion::Instance());

	  return true;
	}

	break;

  case Msg_PassToMe:
	{  
	  
	  //get the position of the player requesting the pass 
	  FieldPlayer* receiver = static_cast<FieldPlayer*>(telegram.ExtraInfo);

	  #ifdef PLAYER_STATE_INFO_ON
	  debug_con << "Player " << player->ID() << " received request from " <<
					receiver->ID() << " to make pass" << "";
	  #endif

	  //if the ball is not within kicking range or their is already a 
	  //receiving player, this player cannot pass the ball to the player
	  //making the request.
	  if (player->Team()->Receiver() != NULL ||
		 !player->BallWithinKickingRange() )
	  {
		#ifdef PLAYER_STATE_INFO_ON
		debug_con << "Player " << player->ID() << " cannot make requested pass <cannot kick ball>" << "";
		#endif

		return true;
	  }
	  
	  //make the pass   
	  player->Ball()->Kick(receiver->Pos() - player->Ball()->Pos(),
						   Prm.MaxPassingForce);

		  
	 #ifdef PLAYER_STATE_INFO_ON
	 debug_con << "Player " << player->ID() << " Passed ball to requesting player" << "";
	 #endif
		
	  //let the receiver know a pass is coming 
	  Dispatcher->DispatchMsg(SEND_MSG_IMMEDIATELY,
							  player->ID(),
							  receiver->ID(),
							  Msg_ReceiveBall,
							  &receiver->Pos());

   

	  //change state   
	  player->GetFSM()->ChangeState(Wait::Instance());

	  player->FindSupport();

	  return true;
	}

	break;

  }//end switch

  return false;
}
								

	   

//***************************************************************************** CHASEBALL

ChaseBall* ChaseBall::Instance()
{
  static ChaseBall instance;

  return &instance;
}


void ChaseBall::Enter(FieldPlayer* player)
{
  player->Steering()->SeekOn();

  #ifdef PLAYER_STATE_INFO_ON
  debug_con << "Player " << player->ID() << " enters chase state" << "";
  #endif
}

void ChaseBall::Execute(FieldPlayer* player)                                     
{
  //if the ball is within kicking range the player changes state to KickBall.
  if (player->BallWithinKickingRange())
  {
	player->GetFSM()->ChangeState(KickBall::Instance());
	
	return;
  }
  //gets the center of the screen, the half way line
  static double ballCenterx = (double)player->Pitch()->m_cxClient / 2.0;

  if (!player->Team()->InControl())
  {
	  //depending on the team side, determine if the ball is on the home side and intercept
	  if (player->Team()->Color() == 1 && (player->Role() == player->defender && player->Pitch()->Ball()->Pos().x < ballCenterx - 100))
	  {
		  player->Steering()->SetTarget(player->Ball()->Pos());

		  return;
	  }
	  else if (player->Team()->Color() == 0 && (player->Role() == player->defender && player->Pitch()->Ball()->Pos().x > ballCenterx + 100))
	  {
		  player->Steering()->SetTarget(player->Ball()->Pos());

		  return;

	  }
  }

																			  
  //if the player is the closest player to the ball then he should keep
  //chasing it
  if (player->isClosestTeamMemberToBall())
  {
	player->Steering()->SetTarget(player->Ball()->Pos());
	return;
  }
  
  //if the player is not closest to the ball anymore, he should return back
  //to his home region and wait for another opportunity
  player->GetFSM()->ChangeState(ReturnToHomeRegion::Instance());
}


void ChaseBall::Exit(FieldPlayer* player)
{
  player->Steering()->SeekOff();
}



//*****************************************************************************SUPPORT ATTACKING PLAYER

SupportAttacker* SupportAttacker::Instance()
{
  static SupportAttacker instance;

  return &instance;
}


void SupportAttacker::Enter(FieldPlayer* player)
{
  player->Steering()->ArriveOn();

  player->Steering()->SetTarget(player->Team()->GetSupportSpot());
  
  #ifdef PLAYER_STATE_INFO_ON
  debug_con << "Player " << player->ID() << " enters support state" << "";
  #endif
}

void SupportAttacker::Execute(FieldPlayer* player)                                     
{
  //if his team loses control go back home
  if (!player->Team()->InControl())
  {
	player->GetFSM()->ChangeState(ReturnToHomeRegion::Instance()); return;
  } 


  //if the best supporting spot changes, change the steering target
  if (player->Team()->GetSupportSpot() != player->Steering()->Target())
  {    
	player->Steering()->SetTarget(player->Team()->GetSupportSpot());

	player->Steering()->ArriveOn();
  }

  //if this player has a shot at the goal AND the attacker can pass
  //the ball to him the attacker should pass the ball to this player
  if( player->Team()->CanShoot(player->Pos(),
							   Prm.MaxShootingForce))
  {
	player->Team()->RequestPass(player);
  }


  //if this player is located at the support spot and his team still have
  //possession, he should remain still and turn to face the ball
  if (player->AtTarget())
  {
	player->Steering()->ArriveOff();
		
	//the player should keep his eyes on the ball!
	player->TrackBall();

	player->SetVelocity(Vector2D(0,0));

	//if not threatened by another player request a pass
	if (!player->isThreatened())
	{
	  player->Team()->RequestPass(player);
	}
  }
}


void SupportAttacker::Exit(FieldPlayer* player)
{
  //set supporting player to null so that the team knows it has to 
  //determine a new one.
  player->Team()->SetSupportingPlayer(NULL);

  player->Steering()->ArriveOff();
}




//************************************************************************ RETURN TO HOME REGION

ReturnToHomeRegion* ReturnToHomeRegion::Instance()
{
  static ReturnToHomeRegion instance;

  return &instance;
}


void ReturnToHomeRegion::Enter(FieldPlayer* player)
{
  player->Steering()->ArriveOn();

  if (!player->HomeRegion()->Inside(player->Steering()->Target(), Region::halfsize))
  {
	player->Steering()->SetTarget(player->HomeRegion()->Center());
  }

  #ifdef PLAYER_STATE_INFO_ON
  debug_con << "Player " << player->ID() << " enters ReturnToHome state" << "";
  #endif
}

void ReturnToHomeRegion::Execute(FieldPlayer* player)
{
  if (player->Pitch()->GameOn())
  {
	//if the ball is nearer this player than any other team member  &&
	//there is not an assigned receiver && the goalkeeper does not gave
	//the ball, go chase it
	if ( player->isClosestTeamMemberToBall() &&
		 (player->Team()->Receiver() == NULL) &&
		 !player->Pitch()->GoalKeeperHasBall())
	{
	  player->GetFSM()->ChangeState(ChaseBall::Instance());

	  return;
	}
  }

  //if game is on and close enough to home, change state to wait and set the 
  //player target to his current position.(so that if he gets jostled out of 
  //position he can move back to it)
  if (player->Pitch()->GameOn() && player->HomeRegion()->Inside(player->Pos(),
															 Region::halfsize))
  {
	player->Steering()->SetTarget(player->Pos());
	player->GetFSM()->ChangeState(Wait::Instance());
  }
  //if game is not on the player must return much closer to the center of his
  //home region
  else if(!player->Pitch()->GameOn() && player->AtTarget())
  {
	player->GetFSM()->ChangeState(Wait::Instance());
  }
}

void ReturnToHomeRegion::Exit(FieldPlayer* player)
{
  player->Steering()->ArriveOff();
}


//***************************************************************************** FATIGUED

Fatigued* Fatigued::Instance()
{
	static Fatigued instance;

	return &instance;
}


void Fatigued::Enter(FieldPlayer* player)
{
#ifdef PLAYER_STATE_INFO_ON
	debug_con << "Player " << player->ID() << " enters fatigued state" << "";
#endif

	//if the game is not on make sure the target is the center of the player's
	//home region. This is ensure all the players are in the correct positions
	//ready for kick off
	if (!player->Pitch()->GameOn())
	{
		player->GetFSM()->ChangeState(ReturnToHomeRegion::Instance());
	}
}

void Fatigued::Execute(FieldPlayer* player)
{
	//if the player doesn't have full stamina, regain until full
	if (player->m_dStaminaRemaining < player->m_dMaxStamina && player->Pitch()->GameOn())
	{
		player->m_dStaminaRemaining += 0.04;
	}
	else
	{
		player->GetFSM()->ChangeState(Wait::Instance());
	}

}

void Fatigued::Exit(FieldPlayer* player) {}


//***************************************************************************** WAIT

Wait* Wait::Instance()
{
  static Wait instance;

  return &instance;
}


void Wait::Enter(FieldPlayer* player)
{
  #ifdef PLAYER_STATE_INFO_ON
  debug_con << "Player " << player->ID() << " enters wait state" << "";
  #endif

  //if the game is not on make sure the target is the center of the player's
  //home region. This is ensure all the players are in the correct positions
  //ready for kick off
  if (!player->Pitch()->GameOn())
  {
	player->Steering()->SetTarget(player->HomeRegion()->Center());
  }
}

void Wait::Execute(FieldPlayer* player)
{    
  //if the player has been jostled out of position, get back in position  
  if (!player->AtTarget())
  {
	player->Steering()->ArriveOn();

	return;
  }

  else
  {
	player->Steering()->ArriveOff();

	player->SetVelocity(Vector2D(0,0));

	//the player should keep his eyes on the ball!
	player->TrackBall();
  }

  //if this player's team is controlling AND this player is not the attacker
  //AND is further up the field than the attacker he should request a pass.
  if ( player->Team()->InControl()    &&
	 (!player->isControllingPlayer()) &&
	   player->isAheadOfAttacker() )
  {
	player->Team()->RequestPass(player);

	return;
  }

  if (player->Pitch()->GameOn())
  {
   //if the ball is nearer this player than any other team member  AND
	//there is not an assigned receiver AND neither goalkeeper has
	//the ball, go chase it
   if (player->Team()->Receiver() == NULL  &&
	   !player->Pitch()->GoalKeeperHasBall())
   {
	   static double ballCenterx = (double)player->Pitch()->m_cxClient / 2.0;

	   if (!player->Team()->InControl())
	   {
		   //if a defender and the ball is on the home side of the pitch, all should chase
		   if (player->Team()->m_pTeamParamFile->Hemming)
		   {
			   if (player->Team()->Color() == 1 && (player->Role() == player->defender && player->Pitch()->Ball()->Pos().x < ballCenterx - 100))
			   {
				   player->GetFSM()->ChangeState(ChaseBall::Instance());
				   return;

			   }
			   else if (player->Team()->Color() == 0 && (player->Role() == player->defender && player->Pitch()->Ball()->Pos().x > ballCenterx + 100))
			   {
				   player->GetFSM()->ChangeState(ChaseBall::Instance());
				   return;
			   }
		   }
		   //if the there is an opponent close to the goal, mark him
		   if (player->Role() == player->defender && player->Team()->isOpponentWithinRadius(player->Team()->HomeGoal()->Center(), player->Team()->m_pTeamParamFile->MarkDist))
		   {
			   player->GetFSM()->ChangeState(Mark::Instance());
			   return;

		   }

	   }

	   if (player->isClosestTeamMemberToBall())
	   {
		   player->GetFSM()->ChangeState(ChaseBall::Instance());
	   }


	 return;
   }
  } 
}

void Wait::Exit(FieldPlayer* player){}




//************************************************************************ KICK BALL

KickBall* KickBall::Instance()
{
  static KickBall instance;

  return &instance;
}


void KickBall::Enter(FieldPlayer* player)
{
  //let the team know this player is controlling
   player->Team()->SetControllingPlayer(player);
   
   //the player can only make so many kick attempts per second.
   if (!player->isReadyForNextKick()) 
   {
	 player->GetFSM()->ChangeState(ChaseBall::Instance());
   }

   
  #ifdef PLAYER_STATE_INFO_ON
  debug_con << "Player " << player->ID() << " enters kick state" << "";
  #endif
}

void KickBall::Execute(FieldPlayer* player)
{ 
  //calculate the dot product of the vector pointing to the ball
  //and the player's heading
  Vector2D ToBall = player->Ball()->Pos() - player->Pos();
  double   dot    = player->Heading().Dot(Vec2DNormalize(ToBall)); 

  //cannot kick the ball if the goalkeeper is in possession or if it is 
  //behind the player or if there is already an assigned receiver. So just
  //continue chasing the ball
  if (player->Team()->Receiver() != NULL   ||
	  player->Pitch()->GoalKeeperHasBall() ||
	  (dot < 0) ) 
  {
	#ifdef PLAYER_STATE_INFO_ON
	debug_con << "Goaly has ball / ball behind player" << "";
	#endif
	
	player->GetFSM()->ChangeState(ChaseBall::Instance());

	return;
  }

  /* Attempt a shot at the goal */

  //if a shot is possible, this vector will hold the position along the 
  //opponent's goal line the player should aim for.
  Vector2D    BallTarget;

  //the dot product is used to adjust the shooting force. The more
  //directly the ball is ahead, the more forceful the kick
  double power = Prm.MaxShootingForce * dot;

  static const double quartStamina = player->m_dMaxStamina / 4;
  static const double halfStamina = player->m_dMaxStamina / 2;
  //reduces the total power of the shot depending on the level of stamina
  if (player->m_dStaminaRemaining <= quartStamina)
  {
	  power /= 4;
  }
  else if (player->m_dStaminaRemaining <= halfStamina)
  {
	  power /= 2;
  }


  //if it is determined that the player could score a goal from this position
  //OR if he should just kick the ball anyway, the player will attempt
  //to make the shot
  if (player->Team()->CanShoot(player->Ball()->Pos(),
							   power,
							   BallTarget)                   || 
	 (RandFloat() < player->Team()->m_pPlayerParamFile->ChancePlayerAttemptsPotShot))
  {
   #ifdef PLAYER_STATE_INFO_ON
   debug_con << "Player " << player->ID() << " attempts a shot at " << BallTarget << "";
   #endif

   //add some noise to the kick. We don't want players who are 
   //too accurate! The amount of noise can be adjusted by altering
   //AIPrm.PlayerKickingAccuracy
   BallTarget = AddNoiseToKick(player->Ball()->Pos(), BallTarget, player);

   //this is the direction the ball will be kicked in
   Vector2D KickDirection = BallTarget - player->Ball()->Pos();
   
   player->Ball()->Kick(KickDirection, power);
	
   //change state   
   player->GetFSM()->ChangeState(Wait::Instance());
   
   player->FindSupport();
  
   return;
 }


  /* Attempt a pass to a player */

  //if a receiver is found this will point to it
  PlayerBase* receiver = NULL;

  power = Prm.MaxPassingForce * dot;

  if (player->m_dStaminaRemaining <= quartStamina)
  {
	  power /= 4;
  }
  else if (player->m_dStaminaRemaining <= halfStamina)
  {
	  power /= 2;
  }

  
  //test if there are any potential candidates available to receive a pass
  if (player->isThreatened()  &&
	  player->Team()->FindPass(player,
							  receiver,
							  BallTarget,
							  power,
		  player->Team()->m_pPlayerParamFile->MinPassDist))
  {     
	//add some noise to the kick
	BallTarget = AddNoiseToKick(player->Ball()->Pos(), BallTarget, player);

	Vector2D KickDirection = BallTarget - player->Ball()->Pos();
   
	player->Ball()->Kick(KickDirection, power);

	#ifdef PLAYER_STATE_INFO_ON
	debug_con << "Player " << player->ID() << " passes the ball with force " << power << "  to player " 
			  << receiver->ID() << "  Target is " << BallTarget << "";
	#endif

	
	//let the receiver know a pass is coming 
	Dispatcher->DispatchMsg(SEND_MSG_IMMEDIATELY,
							player->ID(),
							receiver->ID(),
							Msg_ReceiveBall,
							&BallTarget);                            
   

	//the player should wait at his current position unless instructed
	//otherwise  
	player->GetFSM()->ChangeState(Wait::Instance());

	player->FindSupport();


	return;
  }

  //cannot shoot or pass, so dribble the ball upfield
  else
  {   
	  player->FindSupport();
	  // if the player is close to the opponents goal then wait for support if it cannot shoot
	  if (player->DistToOppGoal() < 200)
	  {
		  player->GetFSM()->ChangeState(Wait::Instance());
	  }
	  else
	  {
		  //dribbles further upfield
		  player->GetFSM()->ChangeState(Dribble::Instance());
	  }

  }   
}


//*************************************************************************** DRIBBLE

Dribble* Dribble::Instance()
{
  static Dribble instance;

  return &instance;
}


void Dribble::Enter(FieldPlayer* player)
{
  //let the team know this player is controlling
  player->Team()->SetControllingPlayer(player);

#ifdef PLAYER_STATE_INFO_ON
  debug_con << "Player " << player->ID() << " enters dribble state" << "";
  #endif
}

void Dribble::Execute(FieldPlayer* player)
{
	//checks if the player is threatened and panic pass to a support
	if (player->isThreatened())
	{
		player->GetFSM()->ChangeState(KickBall::Instance());
	}
	else
	{

		static double ballCenterx = (double)player->Pitch()->m_cxClient / 2.0;
		if (player->Team()->Color() == 0)
		{
			//checks if the defender should pass the ball upward when approaching the middle line 
			if (player->Role() == player->defender && player->Pitch()->Ball()->Pos().x < ballCenterx - 100)
			{
				player->GetFSM()->ChangeState(KickBall::Instance());
				return;
			}
		}
		else 
		{
			if (player->Role() == player->defender && player->Pitch()->Ball()->Pos().x > ballCenterx + 100)
			{
				player->GetFSM()->ChangeState(KickBall::Instance());
				return;
			}
		}

		//if the ball is between the player and the home goal, it needs to swivel
		// the ball around by doing multiple small kicks and turns until the player 
		//is facing in the correct direction
		double dot = player->Team()->HomeGoal()->Facing().Dot(player->Heading());

		if (dot < 0)
		{
			//the player's heading is going to be rotated by a small amount (Pi/4) 
			//and then the ball will be kicked in that direction
			Vector2D direction = player->Heading();




			//calculate the sign (+/-) of the angle between the player heading and the 
			//facing direction of the goal so that the player rotates around in the 
			//correct direction
			double angle = QuarterPi * -1 *
				player->Team()->HomeGoal()->Facing().Sign(player->Heading());


			Vec2DRotateAroundOrigin(direction, angle);

			//this value works well when the player is attempting to control the
			//ball and turn at the same time
			const double KickingForce = 0.8;

			player->Ball()->Kick(direction, KickingForce);
		}

		//kick the ball down the field
		else
		{
			player->Ball()->Kick(player->Team()->HomeGoal()->Facing(),
				Prm.MaxDribbleForce);
		}

		//the player has kicked the ball so he must now change state to follow it
		player->GetFSM()->ChangeState(ChaseBall::Instance());

		return;
	}

 
}



//************************************************************************     RECEIVEBALL

ReceiveBall* ReceiveBall::Instance()
{
  static ReceiveBall instance;

  return &instance;
}


void ReceiveBall::Enter(FieldPlayer* player)
{
  //let the team know this player is receiving the ball
  player->Team()->SetReceiver(player);
  
  //this player is also now the controlling player
  player->Team()->SetControllingPlayer(player);

  //there are two types of receive behavior. One uses arrive to direct
  //the receiver to the position sent by the passer in its telegram. The
  //other uses the pursuit behavior to pursue the ball. 
  //This statement selects between them dependent on the probability
  //ChanceOfUsingArriveTypeReceiveBehavior, whether or not an opposing
  //player is close to the receiving player, and whether or not the receiving
  //player is in the opponents 'hot region' (the third of the pitch closest
  //to the opponent's goal
  const double PassThreatRadius = 70.0;

  if (( player->InHotRegion() ||
		RandFloat() < player->Team()->m_pPlayerParamFile->ChanceOfUsingArriveTypeReceiveBehavior) &&
	 !player->Team()->isOpponentWithinRadius(player->Pos(), PassThreatRadius))
  {
	player->Steering()->ArriveOn();
	
	#ifdef PLAYER_STATE_INFO_ON
	debug_con << "Player " << player->ID() << " enters receive state (Using Arrive)" << "";
	#endif
  }
  else
  {
	player->Steering()->PursuitOn();

	#ifdef PLAYER_STATE_INFO_ON
	debug_con << "Player " << player->ID() << " enters receive state (Using Pursuit)" << "";
	#endif
  }
}

void ReceiveBall::Execute(FieldPlayer* player)
{
  //if the ball comes close enough to the player or if his team lose control
  //he should change state to chase the ball
  if (player->BallWithinReceivingRange() || !player->Team()->InControl())
  {
	player->GetFSM()->ChangeState(ChaseBall::Instance());

	return;
  }  

  if (player->Steering()->PursuitIsOn())
  {
	player->Steering()->SetTarget(player->Ball()->Pos());
  }

  //if the player has 'arrived' at the steering target he should wait and
  //turn to face the ball
  if (player->AtTarget())
  {
	player->Steering()->ArriveOff();
	player->Steering()->PursuitOff();
	player->TrackBall();    
	player->SetVelocity(Vector2D(0,0));
  } 
}

void ReceiveBall::Exit(FieldPlayer* player)
{
  player->Steering()->ArriveOff();
  player->Steering()->PursuitOff();

  player->Team()->SetReceiver(NULL);
}





//***************************************************************************** Mark

Mark* Mark::Instance()
{
	static Mark instance;

	return &instance;
}


void Mark::Enter(FieldPlayer* player)
{
#ifdef PLAYER_STATE_INFO_ON
	debug_con << "Player " << player->ID() << " enters mark state" << "";
#endif

	//if the game is not on make sure the target is the center of the player's
	//home region. This is ensure all the players are in the correct positions
	//ready for kick off
	if (!player->Pitch()->GameOn())
	{
		player->Steering()->SetTarget(player->HomeRegion()->Center());
	}
}

void Mark::Execute(FieldPlayer* player)
{
	if (!player->Team()->InControl())
	{
		//checks if there is any opponent players close to the goal
		if (player->Role() == player->defender && player->Team()->isOpponentWithinRadius(player->Team()->HomeGoal()->Center(), player->Team()->m_pTeamParamFile->MarkDist))
		{
			//gets the player who is close to the goal
			PlayerBase * playerToMark = player->Team()->getOpponentWithinRadius(player->Team()->HomeGoal()->Center(), player->Team()->m_pTeamParamFile->MarkDist);
			
			//sets the player to follow
			player->Steering()->SetTarget(playerToMark->Pos());

			static double ballCenterx = (double)player->Pitch()->m_cxClient / 2.0;

			if (player->Team()->m_pTeamParamFile->Hemming)
			{
				//if a defender and the ball is on the home side of the pitch, all should chase
				if (player->Team()->Color() == 1 && player->Pitch()->Ball()->Pos().x < ballCenterx - 100)
				{
					player->GetFSM()->ChangeState(ChaseBall::Instance());
					return;

				}
				else if (player->Team()->Color() == 0 && player->Pitch()->Ball()->Pos().x > ballCenterx + 100)
				{
					player->GetFSM()->ChangeState(ChaseBall::Instance());
					return;
				}
			}
			else
			{
				if (player->isClosestTeamMemberToBall())
				{
					player->GetFSM()->ChangeState(ChaseBall::Instance());
					return;
				}

			}


			player->Steering()->ArriveOn();

			return;


		}
		else
		{
			player->Steering()->SetTarget(player->HomeRegion()->Center());
			player->GetFSM()->ChangeState(Wait::Instance());

		}
	}
	else
	{
		player->Steering()->SetTarget(player->HomeRegion()->Center());
		player->GetFSM()->ChangeState(Wait::Instance());

	}

}

void Mark::Exit(FieldPlayer* player) {}